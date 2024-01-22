//
// Created by adamyuan on 1/20/24.
//

#pragma once
#ifndef VKHASHDAG_NODEPOOLLIBFORKGC_HPP
#define VKHASHDAG_NODEPOOLLIBFORKGC_HPP

#include "NodePool.hpp"

#include <algorithm>
#include <libfork/schedule/busy_pool.hpp>
#include <libfork/task.hpp>
#include <vector>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word, //
          template <typename, typename> typename HashMap, template <typename> typename HashSet>
class NodePoolThreadedGC {
private:
	using BucketMap = HashMap<Word, std::vector<Word>>;
	using NodeTable = HashMap<Word, Word>;

	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() { return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this)); }

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_tag_bucket(Word level_bucket_base, std::span<const Word> nodes) {
		HashSet<Word> local_node_set;

		for (Word node : nodes) {
			const Word *p_node = get_node_pool().read_node(node);
			Word child_mask = *p_node;
			const Word *p_next_child = p_node + 1;

			for (; child_mask; child_mask &= (child_mask - 1)) {
				Word child = *(p_next_child++);
				if (local_node_set.count(child))
					continue;
				local_node_set.insert(child);
				Word bucket_index = child >> get_node_pool().m_config.GetWordBitsPerBucket();

				std::scoped_lock lock{get_node_pool().get_bucket_mutex(bucket_index)};
				m_bucket_caches[bucket_index - level_bucket_base].push_back(child);
			}
		}
		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_reduce_bucket(std::vector<Word> *p_nodes) {
		auto &nodes = *p_nodes;
		std::sort(nodes.begin(), nodes.end());
		nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<std::vector<BucketMap>, Context> lf_gc_forward_pass(std::vector<Word> root_nodes) {
		std::vector<BucketMap> level_bucket_maps(get_node_pool().m_config.GetNodeLevels());

		// Push root level
		for (Word root : root_nodes) {
			Word bucket = root >> get_node_pool().m_config.GetWordBitsPerBucket();
			level_bucket_maps[0][bucket].push_back(root);
		}

		for (Word level = 1; level < get_node_pool().m_config.GetNodeLevels(); ++level) {
			const auto &prev_bucket_map = level_bucket_maps[level - 1];
			Word level_bucket_base = get_node_pool().m_bucket_level_bases[level];

			if (prev_bucket_map.empty())
				break;

			for (auto it = prev_bucket_map.begin(); it != prev_bucket_map.end();) {
				const auto &nodes = it->second;
				if (++it == prev_bucket_map.end())
					co_await lf_gc_tag_bucket<Context>(level_bucket_base, nodes);
				else
					co_await lf_gc_tag_bucket<Context>(level_bucket_base, nodes).fork();
			}
			co_await lf::join();

			auto &cur_bucket_map = level_bucket_maps[level];
			Word buckets_at_level = get_node_pool().m_config.GetBucketsAtLevel(level);
			for (Word b = 0; b < buckets_at_level; ++b) {
				if (m_bucket_caches[b].empty())
					continue;
				cur_bucket_map[level_bucket_base + b] = std::move(m_bucket_caches[b]);
				m_bucket_caches[b].clear();
			}

			if (cur_bucket_map.empty())
				break;

			for (auto it = cur_bucket_map.begin(); it != cur_bucket_map.end();) {
				auto &nodes = it->second;
				if (++it == cur_bucket_map.end())
					co_await lf_gc_reduce_bucket<Context>(&nodes);
				else
					co_await lf_gc_reduce_bucket<Context>(&nodes).fork();
			}
			co_await lf::join();
		}
		co_return level_bucket_maps;
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context>
	lf_gc_shrink_inner_bucket(Word level, Word level_bucket_base, std::span<const Word> nodes, NodeTable *p_node_table,
	                          Word child_level_bucket_base, const NodeTable *child_node_tables) {
		if (nodes.empty())
			co_return;

		const Config<Word> &config = get_node_pool().m_config;

		Word page_index;
		const Word *page = nullptr;

		std::array<Word, 9> node_cache;

		for (Word node : nodes) {
			// Read node to node cache
			std::span<Word> node_span;
			{
				Word node_page_index = node >> config.word_bits_per_page;
				if (page == nullptr || node_page_index != page_index) {
					page = get_node_pool().read_page(node_page_index);
					page_index = node_page_index;
				}

				Word node_page_offset = node & (config.GetWordsPerPage() - 1u);
				const Word *p_node = page + node_page_offset;
				Word node_words = get_node_pool().get_inner_node_words(p_node);
				std::copy(p_node, p_node + node_words, node_cache.data());
				node_span = {node_cache.data(), node_words};
			}

			// Alter child pointer and Re-Hash
			for (Word i = 1; i < node_span.size(); ++i)
				node_span[i] =
				    child_node_tables[(node_span[i] >> config.GetWordBitsPerBucket()) - child_level_bucket_base].at(
				        node_span[i]);

			const Word new_bucket_index = level_bucket_base + (typename Derived::WordSpanHasher{}(node_span) &
			                                                   (config.GetBucketsAtLevel(level) - 1));

			// Write to bucket cache
			Word new_node;
			{
				std::unique_lock lock{get_node_pool().get_bucket_mutex(new_bucket_index)};

				std::vector<Word> &bucket_cache = m_bucket_caches[new_bucket_index - level_bucket_base];
				Word bucket_cache_offset = bucket_cache.size();

				Word page_slot = bucket_cache_offset >> config.word_bits_per_page; // PageID in bucket
				Word page_offset = bucket_cache_offset & (config.GetWordsPerPage() - 1);

				if (page_offset + node_span.size() > config.GetWordsPerPage()) {
					bucket_cache_offset = (page_slot + 1) << config.word_bits_per_page;
					bucket_cache.resize(bucket_cache_offset + node_span.size());
					std::copy(node_span.begin(), node_span.end(), bucket_cache.begin() + bucket_cache_offset);
				} else
					bucket_cache.insert(bucket_cache.end(), node_span.begin(), node_span.end());

				lock.unlock();

				new_node = (new_bucket_index << config.GetWordBitsPerBucket()) | bucket_cache_offset;
			}

			// Update Node Map
			(*p_node_table)[node] = new_node;
		}

		co_return;
	}

	inline void gc_bucket_free_pages(Word bucket_index, Word bucket_words, Word prev_bucket_words) {
		const Config<Word> &config = get_node_pool().m_config;

		const auto get_upper_page_slot = [&config](Word words) {
			return (words >> config.word_bits_per_page) + (words & (config.GetWordsPerPage() - 1)) ? 1u : 0u;
		};

		Word base_page_index = bucket_index << config.page_bits_per_bucket;
		Word prev_page_slot = get_upper_page_slot(prev_bucket_words);

		for (Word page_slot = get_upper_page_slot(bucket_words); page_slot < prev_page_slot; ++page_slot)
			get_node_pool().free_page(base_page_index | page_slot);
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_flush_inner_bucket(Word bucket_index, std::vector<Word> bucket_cache) {
		const Config<Word> &config = get_node_pool().m_config;

		Word bucket_cache_offset = 0, page_index = bucket_index << config.page_bits_per_bucket;
		for (; bucket_cache_offset + config.GetWordsPerPage() <= bucket_cache.size();
		     bucket_cache_offset += config.GetWordsPerPage(), ++page_index)
			get_node_pool().write_page(
			    page_index, 0,
			    std::span<const Word>{bucket_cache.begin() + bucket_cache_offset, config.GetWordsPerPage()});

		if (bucket_cache_offset < bucket_cache.size())
			get_node_pool().write_page(
			    page_index, 0, std::span<const Word>{bucket_cache.begin() + bucket_cache_offset, bucket_cache.end()});

		Word prev_bucket_words = get_node_pool().get_bucket_words(bucket_index);
		get_node_pool().set_bucket_words(bucket_index, bucket_cache.size());
		gc_bucket_free_pages(bucket_index, bucket_cache.size(), prev_bucket_words);

		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_shrink_leaf_bucket(Word bucket_index, std::span<const Word> nodes,
	                                                              NodeTable *p_node_table) {
		const Config<Word> &config = get_node_pool().m_config;
		// Leaf's hash won't change, so the bucket index remains
		Word prev_bucket_words = get_node_pool().get_bucket_words(bucket_index);
		Word new_bucket_words = 0;

		if (!nodes.empty()) {
			Word page_index;
			const Word *page = nullptr;

			for (Word node : nodes) {
				Word node_page_index = node >> config.word_bits_per_page;
				if (page == nullptr || node_page_index != page_index) {
					page = get_node_pool().read_page(node_page_index);
					page_index = node_page_index;
				}

				Word node_page_offset = node & (config.GetWordsPerPage() - 1u);
				std::span<const Word> leaf_span = {page + node_page_offset, Config<Word>::kWordsPerLeaf};

				NodePointer<Word> new_node_ptr;
				std::tie(new_node_ptr, new_bucket_words) =
				    get_node_pool().append_node(bucket_index, new_bucket_words, leaf_span);

				(*p_node_table)[node] = *new_node_ptr;
			}
		}

		get_node_pool().set_bucket_words(bucket_index, new_bucket_words);
		gc_bucket_free_pages(bucket_index, new_bucket_words, prev_bucket_words);

		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<std::vector<NodeTable>, Context>
	lf_gc_backward_pass(std::vector<BucketMap> level_bucket_maps) {
		const Config<Word> &config = get_node_pool().m_config;

		std::vector<NodeTable> bucket_node_tables[2]; // Ping-pong Node Tables
		{
			Word max_level_buckets = get_max_level_buckets();
			bucket_node_tables[0].resize(max_level_buckets);
			bucket_node_tables[1].resize(max_level_buckets);
		}

		for (Word level = config.GetNodeLevels() - 1; ~level; --level) {
			std::vector<NodeTable> &cur_node_tables = bucket_node_tables[level & 1u];
			BucketMap &bucket_map = level_bucket_maps[level];

			Word buckets_at_level = get_node_pool().m_config.GetBucketsAtLevel(level);
			Word level_bucket_base = get_node_pool().m_bucket_level_bases[level];

			// Leaf
			if (level == config.GetNodeLevels() - 1) {
				for (Word b = 0; b < buckets_at_level; ++b) {
					Word bucket_index = level_bucket_base + b;
					auto it = bucket_map.find(bucket_index);
					std::span<const Word> nodes = it == bucket_map.end() ? std::span<const Word>{} : it->second;

					if (b == buckets_at_level - 1)
						co_await lf_gc_shrink_leaf_bucket<Context>(bucket_index, nodes, cur_node_tables.data() + b);
					else
						co_await lf_gc_shrink_leaf_bucket<Context>(bucket_index, nodes, cur_node_tables.data() + b)
						    .fork();
				}
				co_await lf::join();
			} else {
				auto &child_node_tables = bucket_node_tables[(level & 1u) ^ 1u];
				Word child_level_bucket_base = get_node_pool().m_bucket_level_bases[level + 1];

				for (Word b = 0; b < buckets_at_level; ++b)
					cur_node_tables[b].clear();

				if (!bucket_map.empty()) {
					for (auto it = bucket_map.begin(); it != bucket_map.end();) {
						Word bucket_index = it->first, b = bucket_index - level_bucket_base;
						auto &nodes = it->second;

						if (++it == bucket_map.end())
							co_await lf_gc_shrink_inner_bucket<Context>(
							    level, level_bucket_base, nodes, cur_node_tables.data() + b, child_level_bucket_base,
							    child_node_tables.data());
						else
							co_await lf_gc_shrink_inner_bucket<Context>(
							    level, level_bucket_base, nodes, cur_node_tables.data() + b, child_level_bucket_base,
							    child_node_tables.data())
							    .fork();
					}
					co_await lf::join();
				}

				for (Word b = 0; b < buckets_at_level; ++b) {
					Word bucket_index = level_bucket_base + b;
					std::vector<Word> bucket_cache = std::move(m_bucket_caches[b]);
					m_bucket_caches[b].clear();

					if (b == buckets_at_level - 1)
						co_await lf_gc_flush_inner_bucket<Context>(bucket_index, std::move(bucket_cache));
					else
						co_await lf_gc_flush_inner_bucket<Context>(bucket_index, std::move(bucket_cache)).fork();
				}
				co_await lf::join();
			}
		}
		co_return std::move(bucket_node_tables[0]);
	}

	inline Word get_max_level_buckets() const {
		Word max_level_buckets = 0;
		for (Word level = 0; level < get_node_pool().m_config.GetNodeLevels(); ++level)
			max_level_buckets = std::max(max_level_buckets, get_node_pool().m_config.GetBucketsAtLevel(level));
		return max_level_buckets;
	}

	inline std::vector<Word> gc_make_root_nodes(std::span<const NodePointer<Word>> root_ptrs) {
		std::vector<Word> roots;
		roots.reserve(root_ptrs.size() + 1);
		// Preserve root for filled nodes
		if (!get_node_pool().m_filled_node_pointers.empty() && get_node_pool().m_filled_node_pointers.front())
			roots.push_back(*get_node_pool().m_filled_node_pointers.front());

		// Push Non-Null roots
		for (NodePointer<Word> root_ptr : root_ptrs)
			if (root_ptr)
				roots.push_back(*root_ptr);

		// Sort and remove duplicates
		std::sort(roots.begin(), roots.end());
		roots.erase(std::unique(roots.begin(), roots.end()), roots.end());

		return roots;
	}

	inline void gc_threaded(lf::busy_pool *p_lf_pool, std::span<NodePointer<Word>> root_ptrs) {
		const Config<Word> &config = get_node_pool().m_config;

		auto roots = gc_make_root_nodes(root_ptrs);
		auto level_bucket_maps = p_lf_pool->schedule(lf_gc_forward_pass<lf::busy_pool::context>(std::move(roots)));
		auto node_tables =
		    p_lf_pool->schedule(lf_gc_backward_pass<lf::busy_pool::context>(std::move(level_bucket_maps)));
		for (auto &root_ptr : root_ptrs)
			if (root_ptr)
				root_ptr = NodePointer<Word>(node_tables[*root_ptr >> config.GetWordBitsPerBucket()].at(*root_ptr));
		// Re-initialize filled node pointers if altered
		if (!get_node_pool().m_filled_node_pointers.empty()) {
			get_node_pool().m_filled_node_pointers.clear();
			get_node_pool().make_filled_node_pointers();
		}
	}

	std::vector<std::vector<Word>> m_bucket_caches;

public:
	inline NodePoolThreadedGC() {
		static_assert(std::is_base_of_v<NodePoolBase<Derived, Word>, Derived>);
		m_bucket_caches.resize(get_max_level_buckets());
	}

	inline NodePointer<Word> ThreadedGC(lf::busy_pool *p_lf_pool, NodePointer<Word> root_ptr) {
		gc_threaded(p_lf_pool, std::span<NodePointer<Word>>{&root_ptr, 1});
		return root_ptr;
	}

	inline std::vector<NodePointer<Word>> ThreadedGC(lf::busy_pool *p_lf_pool,
	                                                 std::vector<NodePointer<Word>> root_ptrs) {
		gc_threaded(p_lf_pool, root_ptrs);
		return std::move(root_ptrs);
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLLIBFORKGC_HPP

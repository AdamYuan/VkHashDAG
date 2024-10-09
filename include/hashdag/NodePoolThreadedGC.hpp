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
	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() { return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this)); }
	inline const Config<Word> &get_config() const { return get_node_pool().m_config; }
	inline Word get_bucket_base(Word level) const { return get_node_pool().m_bucket_level_bases[level]; }

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_tag_node(Word level, std::span<std::vector<Word>> src_bucket_nodes,
	                                                    std::span<HashSet<Word>> worker_node_sets) {
		auto context = co_await lf::get_context();
		HashSet<Word> &worker_node_set = worker_node_sets[context->get_worker_id()];

		bool leaf = level == get_config().GetNodeLevels() - 1;
		for (std::vector<Word> &nodes : src_bucket_nodes) {
			if (nodes.empty())
				continue;

			std::sort(nodes.begin(), nodes.end());
			nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());

			if (!leaf) {
				for (Word node : nodes) {
					const Word *p_node = get_node_pool().read_node(node);
					Word child_mask = *p_node;
					const Word *p_next_child = p_node + 1;

					for (; child_mask; child_mask &= (child_mask - 1)) {
						Word child = *(p_next_child++);
						if (worker_node_set.count(child))
							continue;
						worker_node_set.insert(child);
						Word child_bucket = child >> get_config().GetWordBitsPerBucket();

						std::scoped_lock lock{get_node_pool().get_bucket_ref_mutex(child_bucket)};
						m_bucket_nodes[child_bucket].push_back(child);
					}
				}
			}
		}
		co_return;
	}

	inline std::pair<Word, Word> get_level_loop_bits(Word level) const {
		Word bucket_bits = get_config().bucket_bits_each_level[level];
		Word loop_bits = std::min(m_parallel_bits, bucket_bits);
		Word block_bits = bucket_bits - loop_bits;
		return {loop_bits, block_bits};
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_forward_pass(std::vector<Word> root_nodes) {
		// Push root level
		for (Word root : root_nodes) {
			Word bucket = root >> get_config().GetWordBitsPerBucket();
			m_bucket_nodes[bucket].push_back(root);
		}

		auto context = co_await lf::get_context();
		std::vector<HashSet<Word>> worker_node_sets(context->get_worker_count());

		for (Word level = 0; level < get_config().GetNodeLevels(); ++level) {
			auto [loop_bits, block_bits] = get_level_loop_bits(level);
			Word bucket_base = get_bucket_base(level);
			bool leaf = level == get_config().GetNodeLevels() - 1;

			for (HashSet<Word> &node_set : worker_node_sets)
				node_set.clear();

			for (Word i = 0; i < (1u << loop_bits); ++i) {
				std::span<std::vector<Word>> src_bucket_nodes = {
				    m_bucket_nodes.data() + bucket_base + (i << block_bits), (1u << block_bits)};

				if (i + 1 == (1u << loop_bits))
					co_await lf_gc_tag_node<Context>(level, src_bucket_nodes, worker_node_sets);
				else
					co_await lf_gc_tag_node<Context>(level, src_bucket_nodes, worker_node_sets).fork();
			}
			co_await lf::join();
		}
		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context>
	lf_gc_shrink_inner_bucket(Word level, std::span<std::vector<Word>> bucket_nodes,
	                          std::span<const HashMap<Word, Word>> child_node_tables,
	                          HashMap<Word, Word> *p_node_table) {
		const Config<Word> &config = get_node_pool().m_config;

		Word bucket_base = get_bucket_base(level), child_bucket_base = get_bucket_base(level + 1);
		auto [_, child_block_bits] = get_level_loop_bits(level + 1);

		std::array<Word, 9> node_cache;

		for (std::vector<Word> &nodes : bucket_nodes) {
			if (nodes.empty())
				continue;

			Word page_index;
			const Word *page = nullptr;

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
				for (Word i = 1; i < node_span.size(); ++i) {
					Word child_bucket_slot = (node_span[i] >> config.GetWordBitsPerBucket()) - child_bucket_base;
					node_span[i] = child_node_tables[child_bucket_slot >> child_block_bits].at(node_span[i]);
				}

				const Word new_bucket = bucket_base + (typename Derived::WordSpanHasher{}(node_span) &
				                                       (config.GetBucketsAtLevel(level) - 1));

				// Write to bucket cache
				Word new_node;
				{
					std::unique_lock lock{get_node_pool().get_bucket_ref_mutex(new_bucket)};

					std::vector<Word> &bucket_cache = m_bucket_caches[new_bucket - bucket_base];
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

					new_node = (new_bucket << config.GetWordBitsPerBucket()) | bucket_cache_offset;
				}

				// Update Node Map
				(*p_node_table)[node] = new_node;
			}

			nodes.clear();
			nodes.shrink_to_fit();
		}

		co_return;
	}

	inline void gc_bucket_free_pages(Word bucket, Word bucket_words, Word prev_bucket_words) {
		const Config<Word> &config = get_node_pool().m_config;

		const auto get_upper_page_slot = [&config](Word words) {
			return (words >> config.word_bits_per_page) + (words & (config.GetWordsPerPage() - 1)) ? 1u : 0u;
		};

		Word base_page_index = bucket << config.page_bits_per_bucket;
		Word prev_page_slot = get_upper_page_slot(prev_bucket_words);

		for (Word page_slot = get_upper_page_slot(bucket_words); page_slot < prev_page_slot; ++page_slot)
			get_node_pool().free_page(base_page_index | page_slot);
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_flush_inner_bucket(Word first_bucket,
	                                                              std::span<std::vector<Word>> bucket_caches) {
		const Config<Word> &config = get_node_pool().m_config;

		Word bucket = first_bucket;
		for (std::vector<Word> &bucket_cache : bucket_caches) {
			Word bucket_cache_offset = 0, page_index = bucket << config.page_bits_per_bucket;
			for (; bucket_cache_offset + config.GetWordsPerPage() <= bucket_cache.size();
			     bucket_cache_offset += config.GetWordsPerPage(), ++page_index)
				get_node_pool().write_page(
				    page_index, 0,
				    std::span<const Word>{bucket_cache.begin() + bucket_cache_offset, config.GetWordsPerPage()});

			if (bucket_cache_offset < bucket_cache.size())
				get_node_pool().write_page(
				    page_index, 0,
				    std::span<const Word>{bucket_cache.begin() + bucket_cache_offset, bucket_cache.end()});

			Word &ref_bucket_words = get_node_pool().get_bucket_ref_words(bucket);
			Word prev_bucket_words = ref_bucket_words;
			ref_bucket_words = bucket_cache.size();
			gc_bucket_free_pages(bucket, bucket_cache.size(), prev_bucket_words);

			++bucket;
			bucket_cache.clear();
		}

		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_shrink_leaf_bucket(Word first_bucket,
	                                                              std::span<std::vector<Word>> bucket_nodes,
	                                                              HashMap<Word, Word> *p_node_table) {
		const Config<Word> &config = get_node_pool().m_config;

		Word bucket = first_bucket;

		// Leaf's hash won't change, so the bucket index remains
		for (std::vector<Word> &nodes : bucket_nodes) {
			Word &ref_bucket_words = get_node_pool().get_bucket_ref_words(bucket);
			Word prev_bucket_words = ref_bucket_words; // Read bucket_words
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
					    get_node_pool().append_node(bucket, new_bucket_words, leaf_span);

					(*p_node_table)[node] = *new_node_ptr;
				}
			}

			ref_bucket_words = new_bucket_words; // Write bucket_words
			gc_bucket_free_pages(bucket, new_bucket_words, prev_bucket_words);

			++bucket;
			nodes.clear();
			nodes.shrink_to_fit();
		}

		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_backward_pass(std::span<NodePointer<Word>> root_ptrs) {
		const Config<Word> &config = get_node_pool().m_config;

		std::vector<HashMap<Word, Word>> ping_pong_node_tables[2]; // Ping-pong Node Tables
		ping_pong_node_tables[0].resize(1u << m_parallel_bits);
		ping_pong_node_tables[1].resize(1u << m_parallel_bits);

		for (Word level = config.GetNodeLevels() - 1; ~level; --level) {
			std::vector<HashMap<Word, Word>> &cur_node_tables = ping_pong_node_tables[level & 1u];

			auto [loop_bits, block_bits] = get_level_loop_bits(level);
			Word bucket_base = get_bucket_base(level);

			// Leaf
			if (level == config.GetNodeLevels() - 1) {
				for (Word i = 0; i < (1u << loop_bits); ++i) {
					Word first_bucket = bucket_base + (i << block_bits);
					std::span<std::vector<Word>> src_bucket_nodes = {m_bucket_nodes.data() + first_bucket,
					                                                 (1u << block_bits)};

					if (i + 1 == (1u << loop_bits))
						co_await lf_gc_shrink_leaf_bucket<Context>(first_bucket, src_bucket_nodes,
						                                           cur_node_tables.data() + i);
					else
						co_await lf_gc_shrink_leaf_bucket<Context>(first_bucket, src_bucket_nodes,
						                                           cur_node_tables.data() + i)
						    .fork();
				}
				co_await lf::join();
			} else {
				for (auto &table : cur_node_tables)
					table.clear();

				std::vector<HashMap<Word, Word>> &child_node_tables = ping_pong_node_tables[(level & 1u) ^ 1u];
				for (Word i = 0; i < (1u << loop_bits); ++i) {
					Word first_bucket = bucket_base + (i << block_bits);
					std::span<std::vector<Word>> src_bucket_nodes = {m_bucket_nodes.data() + first_bucket,
					                                                 (1u << block_bits)};

					if (i + 1 == (1u << loop_bits))
						co_await lf_gc_shrink_inner_bucket<Context>(level, src_bucket_nodes, child_node_tables,
						                                            cur_node_tables.data() + i);
					else
						co_await lf_gc_shrink_inner_bucket<Context>(level, src_bucket_nodes, child_node_tables,
						                                            cur_node_tables.data() + i)
						    .fork();
				}
				co_await lf::join();

				for (Word i = 0; i < (1u << loop_bits); ++i) {
					Word first_bucket = bucket_base + (i << block_bits);
					std::span<std::vector<Word>> bucket_caches = {m_bucket_caches.data() + (i << block_bits),
					                                              (1u << block_bits)};

					if (i + 1 == (1u << loop_bits))
						co_await lf_gc_flush_inner_bucket<Context>(first_bucket, bucket_caches);
					else
						co_await lf_gc_flush_inner_bucket<Context>(first_bucket, bucket_caches).fork();
				}
				co_await lf::join();
			}

			if (level == 0) {
				for (auto &root_ptr : root_ptrs)
					if (root_ptr)
						root_ptr = NodePointer<Word>(
						    cur_node_tables[*root_ptr >> (config.GetWordBitsPerBucket() + block_bits)].at(*root_ptr));
			}
		}

		co_return;
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

		return roots;
	}

	inline void gc_threaded(lf::busy_pool *p_lf_pool, std::span<NodePointer<Word>> root_ptrs) {
		m_parallel_bits = std::bit_width(p_lf_pool->get_worker_count()) << 1u;

		p_lf_pool->schedule(lf_gc_forward_pass<lf::busy_pool::context>(gc_make_root_nodes(root_ptrs)));
		p_lf_pool->schedule(lf_gc_backward_pass<lf::busy_pool::context>(root_ptrs));
		// Re-initialize filled node pointers if altered
		if (!get_node_pool().m_filled_node_pointers.empty()) {
			get_node_pool().m_filled_node_pointers.clear();
			get_node_pool().make_filled_node_pointers();
		}
	}

	Word m_parallel_bits = -1;
	std::vector<std::vector<Word>> m_bucket_nodes, m_bucket_caches;

public:
	inline NodePoolThreadedGC() {
		static_assert(std::is_base_of_v<NodePoolBase<Derived, Word>, Derived>);
		m_bucket_nodes.resize(get_config().GetTotalBuckets());
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

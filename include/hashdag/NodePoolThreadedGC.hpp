//
// Created by adamyuan on 1/20/24.
//

#pragma once
#ifndef VKHASHDAG_NODEPOOLLIBFORKGC_HPP
#define VKHASHDAG_NODEPOOLLIBFORKGC_HPP

#include "NodePool.hpp"

#include <algorithm>
#include <cuckoohash_map.hh>
#include <libfork/schedule/busy_pool.hpp>
#include <libfork/task.hpp>
#include <mutex>
#include <set>
#include <unordered_set>
#include <vector>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word> class NodePoolThreadedGC {
private:
	using LevelBucketNodes = std::vector<std::unordered_map<Word, std::vector<Word>>>;

	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() { return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this)); }

	struct ParallelNodes {
		std::vector<Word> nodes;
		std::mutex mutex;

		inline void Push(Word node) {
			std::scoped_lock lock{mutex};
			nodes.push_back(node);
		}
	};

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_tag_bucket_nodes(Word bucket_base, std::span<const Word> nodes,
	                                                            ParallelNodes *parallel_bucket_nodes) const {
		std::unordered_set<Word> local_node_set;

		for (Word node : nodes) {
			const Word *p_node = get_node_pool().read_node(node);
			Word child_mask = *p_node;
			const Word *p_next_child = p_node + 1;

			for (; child_mask; child_mask &= (child_mask - 1)) {
				Word child = *(p_next_child++);
				if (local_node_set.count(child))
					continue;
				local_node_set.insert(child);
				Word bucket = child / get_node_pool().m_config.GetWordsPerBucket();
				parallel_bucket_nodes[bucket - bucket_base].Push(child);
			}
		}
		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_pop_bucket_nodes(std::vector<Word> src_nodes,
	                                                            std::vector<Word> *p_dst_nodes) const {
		std::sort(src_nodes.begin(), src_nodes.end());
		src_nodes.erase(std::unique(src_nodes.begin(), src_nodes.end()), src_nodes.end());
		*p_dst_nodes = std::move(src_nodes);
		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<LevelBucketNodes, Context> lf_gc_forward_pass(std::vector<Word> root_nodes) const {
		LevelBucketNodes level_bucket_nodes(get_node_pool().m_config.GetNodeLevels());
		{
			std::sort(root_nodes.begin(), root_nodes.end());
			for (Word root : root_nodes) {
				Word bucket = root / get_node_pool().m_config.GetWordsPerBucket();
				level_bucket_nodes[0][bucket].push_back(root);
			}
		}
		{
			std::unique_ptr<ParallelNodes[]> parallel_bucket_nodes;
			{
				Word max_level_buckets = 0;
				for (Word level = 1; level < get_node_pool().m_config.GetNodeLevels(); ++level) {
					Word buckets_at_level = get_node_pool().m_config.GetBucketsAtLevel(level);
					max_level_buckets = std::max(max_level_buckets, buckets_at_level);
				}
				parallel_bucket_nodes = std::make_unique<ParallelNodes[]>(max_level_buckets);
			}

			for (Word level = 1; level < get_node_pool().m_config.GetNodeLevels(); ++level) {
				const auto &prev_bucket_nodes = level_bucket_nodes[level - 1];
				Word bucket_base = get_node_pool().m_bucket_level_bases[level];

				for (auto it = prev_bucket_nodes.begin(); it != prev_bucket_nodes.end();) {
					const auto &nodes = it->second;
					if (++it == prev_bucket_nodes.end())
						co_await lf_gc_tag_bucket_nodes<Context>(bucket_base, nodes, parallel_bucket_nodes.get());
					else
						co_await lf_gc_tag_bucket_nodes<Context>(bucket_base, nodes, parallel_bucket_nodes.get())
						    .fork();
				}
				co_await lf::join();

				auto &cur_bucket_nodes = level_bucket_nodes[level];
				Word buckets_at_level = get_node_pool().m_config.GetBucketsAtLevel(level);
				for (Word b = 0; b < buckets_at_level; ++b) {
					if (parallel_bucket_nodes[b].nodes.empty())
						continue;

					Word bucket_idx = bucket_base + b;
					cur_bucket_nodes[bucket_idx] = std::move(parallel_bucket_nodes[b].nodes);
				}

				for (auto it = cur_bucket_nodes.begin(); it != cur_bucket_nodes.end();) {
					auto src_nodes = std::move(it->second);
					auto &dst_nodes = it->second;

					if (++it == cur_bucket_nodes.end())
						co_await lf_gc_pop_bucket_nodes<Context>(std::move(src_nodes), &dst_nodes);
					else
						co_await lf_gc_pop_bucket_nodes<Context>(std::move(src_nodes), &dst_nodes).fork();
				}
				co_await lf::join();

				printf("%d %zu\n", level, cur_bucket_nodes.size());
			}
		}
		co_return level_bucket_nodes;
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_backward_pass(std::vector<Word> root_nodes) const {

	}

	inline std::vector<Word> gc_make_root_nodes(std::span<const NodePointer<Word>> root_ptrs) const {
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

public:
	inline NodePoolThreadedGC() { static_assert(std::is_base_of_v<NodePoolBase<Derived, Word>, Derived>); }

	inline NodePointer<Word> ThreadedGC(lf::busy_pool *p_lf_pool, NodePointer<Word> root_ptr) {
		auto roots = gc_make_root_nodes({&root_ptr, 1});
		if (roots.empty())
			return {};
		auto bucket_node_sets = p_lf_pool->schedule(lf_gc_forward_pass<lf::busy_pool::context>(std::move(roots)));
		return {};
	}

	inline std::vector<NodePointer<Word>> ThreadedGC(lf::busy_pool *p_lf_pool,
	                                                 std::span<const NodePointer<Word>> root_ptrs) {
		auto roots = gc_make_root_nodes(root_ptrs);
		if (roots.empty())
			return {};
		auto bucket_node_sets = p_lf_pool->schedule(lf_gc_forward_pass<lf::busy_pool::context>(std::move(roots)));
		return {};
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLLIBFORKGC_HPP

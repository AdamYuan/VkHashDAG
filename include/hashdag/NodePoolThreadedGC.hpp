//
// Created by adamyuan on 1/20/24.
//

#pragma once
#ifndef VKHASHDAG_NODEPOOLLIBFORKGC_HPP
#define VKHASHDAG_NODEPOOLLIBFORKGC_HPP

#include "NodePool.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <unordered_set>
#include <vector>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word> class NodePoolThreadedGC {
private:
	struct BucketNodes {
		std::vector<Word> nodes;
		inline Word GetBucket(const Config<Word> &config) {
			return nodes.front() >> (config.page_bits_per_bucket + config.word_bits_per_page);
		}
	};
	using LevelBucketNodes = std::vector<std::vector<BucketNodes>>;

	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() { return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this)); }

	// TODO: replace with some lock-free push_back DS
	struct ParallelBucketNodes {
		std::vector<Word> nodes;
		std::mutex mutex;

		inline void Push(Word node) {
			std::scoped_lock lock{mutex};
			nodes.push_back(node);
		}
	};

	inline void gc_parallel_tag_bucket_nodes(Word bucket_base, std::span<const BucketNodes> bucket_nodes,
	                                         std::span<ParallelBucketNodes> parallel_bucket_nodes,
	                                         std::size_t concurrency, auto &&async_func) {
		std::atomic_size_t counter = 0;

		std::vector<std::future<void>> futures(concurrency);
		for (auto &f : futures) {
			f = async_func([&]() {
				std::unordered_set<Word> local_node_set;

				for (;;) {
					std::size_t index = counter.fetch_add(1, std::memory_order_relaxed);
					if (index >= bucket_nodes.size())
						break;

					for (Word node : bucket_nodes[index].nodes) {
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
				}
			});
		}
		for (auto &f : futures)
			f.wait();
	}

	inline std::vector<BucketNodes>
	gc_reduce_parallel_bucket_nodes(std::span<ParallelBucketNodes> parallel_bucket_nodes, std::size_t concurrency,
	                                auto &&async_func) {
		std::atomic_size_t counter = 0, bn_counter = 0;
		std::vector<BucketNodes> bucket_nodes(parallel_bucket_nodes.size());

		std::vector<std::future<void>> futures(concurrency);
		for (auto &f : futures) {
			f = async_func([&]() {
				for (;;) {
					std::size_t index = counter.fetch_add(1, std::memory_order_relaxed);
					if (index >= parallel_bucket_nodes.size())
						break;

					if (parallel_bucket_nodes[index].nodes.empty()) {
						// TODO: Delete page
					} else {
						std::size_t bn_index = bn_counter.fetch_add(1, std::memory_order_relaxed);
						auto &nodes = bucket_nodes[bn_index].nodes;
						nodes = std::move(parallel_bucket_nodes[index].nodes);
						std::sort(nodes.begin(), nodes.end());
						nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
					}
				}
			});
		}
		for (auto &f : futures)
			f.wait();

		bucket_nodes.resize(bn_counter.load(std::memory_order_acquire));
		bucket_nodes.shrink_to_fit();

		return bucket_nodes;
	}

	inline LevelBucketNodes gc_forward_pass(std::vector<Word> root_nodes, std::size_t concurrency, auto &&async_func) {
		LevelBucketNodes level_bucket_nodes(get_node_pool().m_config.GetNodeLevels());
		{
			std::sort(root_nodes.begin(), root_nodes.end());
			for (Word root : root_nodes) {
				Word bucket = root / get_node_pool().m_config.GetWordsPerBucket();
				if (level_bucket_nodes[0].empty() ||
				    level_bucket_nodes[0].back().GetBucket(get_node_pool().m_config) != bucket)
					level_bucket_nodes[0].push_back(BucketNodes{.nodes = {root}});
				else
					level_bucket_nodes[0].back().nodes.push_back(root);
			}
		}
		{
			// Initialize parallel bucket nodes (cache)
			std::unique_ptr<ParallelBucketNodes[]> parallel_bucket_nodes;
			{
				Word max_level_buckets = 0;
				for (Word level = 1; level < get_node_pool().m_config.GetNodeLevels(); ++level) {
					Word buckets_at_level = get_node_pool().m_config.GetBucketsAtLevel(level);
					max_level_buckets = std::max(max_level_buckets, buckets_at_level);
				}
				parallel_bucket_nodes = std::make_unique<ParallelBucketNodes[]>(max_level_buckets);
			}

			for (Word level = 1; level < get_node_pool().m_config.GetNodeLevels(); ++level) {
				const auto &prev_bucket_nodes = level_bucket_nodes[level - 1];
				Word bucket_base = get_node_pool().m_bucket_level_bases[level];
				Word buckets_at_level = get_node_pool().m_config.GetBucketsAtLevel(level);
				std::span<ParallelBucketNodes> parallel_bucket_node_span = {parallel_bucket_nodes.get(),
				                                                            buckets_at_level};

				gc_parallel_tag_bucket_nodes(bucket_base, prev_bucket_nodes, parallel_bucket_node_span, concurrency,
				                             async_func);

				level_bucket_nodes[level] =
				    gc_reduce_parallel_bucket_nodes(parallel_bucket_node_span, concurrency, async_func);

				printf("%d %zu\n", level, level_bucket_nodes[level].size());
			}
		}
		return level_bucket_nodes;
	}

	inline void gc_backward_pass(LevelBucketNodes level_bucket_nodes, std::size_t concurrency,
	                             auto &&async_func = std::async) const {
		for (Word level = get_node_pool().m_config.GetNodeLevels() - 1; ~level; --level) {
			Word buckets_at_level = get_node_pool().m_config.GetBucketsAtLevel(level);
		}
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

	inline NodePointer<Word> ThreadedGC(NodePointer<Word> root_ptr, std::size_t concurrency, auto &&async_func) {
		auto roots = gc_make_root_nodes({&root_ptr, 1});
		if (roots.empty())
			return {};
		auto level_bucket_nodes = gc_forward_pass(std::move(roots), concurrency, async_func);
		return {};
	}

	inline std::vector<NodePointer<Word>> ThreadedGC(std::span<const NodePointer<Word>> root_ptrs,
	                                                 std::size_t concurrency, auto &&async_func = std::async) {
		auto roots = gc_make_root_nodes(root_ptrs);
		if (roots.empty())
			return {};
		auto level_bucket_nodes = gc_forward_pass(std::move(roots), concurrency, async_func);
		return {};
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLLIBFORKGC_HPP

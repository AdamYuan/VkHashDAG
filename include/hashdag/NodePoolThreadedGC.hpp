//
// Created by adamyuan on 1/20/24.
//

#pragma once
#ifndef VKHASHDAG_NODEPOOLLIBFORKGC_HPP
#define VKHASHDAG_NODEPOOLLIBFORKGC_HPP

#include "NodePool.hpp"

#include <cuckoohash_map.hh>
#include <libfork/schedule/busy_pool.hpp>
#include <libfork/task.hpp>
#include <set>
#include <variant>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word> class NodePoolThreadedGC {
private:
	using NodeSet = std::set<Word>;
	using BucketNodeSet = libcuckoo::cuckoohash_map<Word, NodeSet>;

	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() { return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this)); }

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_gc_tag_node_set(const NodeSet *p_node_set,
	                                                        BucketNodeSet *p_bucket_node_set) const {
		for (Word node : *p_node_set) {
			const Word *p_node = get_node_pool().read_node(node);
			Word child_mask = *p_node;
			const Word *p_next_child = p_node + 1;

			for (; child_mask; child_mask &= (child_mask - 1)) {
				Word child = *(p_next_child++);
				Word bucket = child / get_node_pool().m_config.GetWordsPerBucket();
				p_bucket_node_set->upsert(
				    bucket, [child](std::set<Word> &node_set, libcuckoo::UpsertContext) { node_set.insert(child); });
			}
		}
		co_return;
	}

	template <lf::context Context>
	inline lf::basic_task<std::unique_ptr<BucketNodeSet[]>, Context>
	lf_gc_forward_pass(std::vector<Word> root_nodes) const {
		std::unique_ptr<BucketNodeSet[]> bucket_node_sets =
		    std::make_unique<BucketNodeSet[]>(get_node_pool().m_config.GetNodeLevels());

		{
			auto locked_bucket_node_set = bucket_node_sets[0].lock_table();
			for (Word root : root_nodes) {
				Word bucket = root / get_node_pool().m_config.GetWordsPerBucket();
				locked_bucket_node_set[bucket].insert(root);
			}
		}
		for (Word level = 1; level < get_node_pool().m_config.GetNodeLevels(); ++level) {
			auto locked_bucket_node_set = bucket_node_sets[level - 1].lock_table();
			for (auto it = locked_bucket_node_set.begin(); it != locked_bucket_node_set.end();) {
				const NodeSet &node_set = it->second;
				if (++it == locked_bucket_node_set.end())
					co_await lf_gc_tag_node_set<Context>(&node_set, bucket_node_sets.get() + level);
				else
					co_await lf_gc_tag_node_set<Context>(&node_set, bucket_node_sets.get() + level).fork();
			}
			co_await lf::join();
		}
		co_return bucket_node_sets;
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
		p_lf_pool->schedule(lf_gc_forward_pass<lf::busy_pool::context>(gc_make_root_nodes({&root_ptr, 1})));
		return {};
	}

	inline std::vector<NodePointer<Word>> ThreadedGC(lf::busy_pool *p_lf_pool,
	                                                 std::span<const NodePointer<Word>> root_ptrs) {
		p_lf_pool->schedule(lf_gc_forward_pass<lf::busy_pool::context>(gc_make_root_nodes(root_ptrs)));
		return {};
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLLIBFORKGC_HPP

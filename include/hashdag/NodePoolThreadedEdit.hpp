//
// Created by adamyuan on 1/17/24.
//

// Threaded NodePool Edit with libfork

#pragma once
#ifndef VKHASHDAG_NODEPOOLTHREADEDEDIT_HPP
#define VKHASHDAG_NODEPOOLTHREADEDEDIT_HPP

#include "NodePool.hpp"

#include <libfork/schedule/busy_pool.hpp>
#include <libfork/task.hpp>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word> class NodePoolThreadedEdit {
private:
	inline const NodePoolBase<Derived, Word> &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline NodePoolBase<Derived, Word> &get_node_pool() {
		return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this));
	}

	template <lf::context Context, Editor<Word> Editor_T>
	inline lf::basic_task<void, Context> lf_edit_node(const Editor_T *p_editor, NodePointer<Word> *p_node_ptr,
	                                                  NodeCoord<Word> coord, auto *p_state, Word max_task_level) {
		NodePointer<Word> node_ptr = *p_node_ptr;
		if (coord.level == get_node_pool().m_config.GetNodeLevels() - 1) {
			*p_node_ptr = get_node_pool().template edit_leaf<true>(*p_editor, node_ptr, coord, p_state);
			co_return;
		}
		if (coord.level >= max_task_level) {
			*p_node_ptr = get_node_pool().template edit_node<true>(*p_editor, node_ptr, coord, p_state);
			co_return;
		}

		std::array<Word, 9> unpacked_node = get_node_pool().get_unpacked_node_array(node_ptr);

		Word &child_mask = unpacked_node[0];
		std::span<Word, 8> children = std::span<Word, 9>{unpacked_node}.template subspan<1>();

		std::array<NodePointer<Word>, 8> new_children;
		std::array<typename Editor_T::NodeState, 8> child_states;

		Word fork_count = 0;
		std::array<Word, 8> fork_indices;

		for (Word i = 0; i < 8; ++i) {
			NodePointer<Word> child_ptr{children[i]};
			NodeCoord<Word> child_coord = coord.GetChildCoord(i);
			get_node_pool().edit_switch(
			    *p_editor, child_ptr, child_coord, p_state,
			    [&](typename Editor_T::NodeState &&child_state, NodePointer<Word> new_child_ptr) {
				    child_states[i] = std::move(child_state);
				    new_children[i] = new_child_ptr;
			    },
			    [&](typename Editor_T::NodeState &&child_state) {
				    child_states[i] = std::move(child_state);
				    fork_indices[fork_count++] = i;
			    });
		}

		// Fork
		if (fork_count) {
			for (Word count = 0; Word i : std::span{fork_indices.data(), fork_count}) {
				++count;
				NodePointer<Word> child_ptr{children[i]};
				NodeCoord<Word> child_coord = coord.GetChildCoord(i);
				auto &child_state = child_states[i];

				new_children[i] = child_ptr;
				if (count == fork_count)
					co_await lf_edit_node<Context>(p_editor, new_children.data() + i, child_coord, &child_state,
					                               max_task_level);
				else
					co_await lf_edit_node<Context>(p_editor, new_children.data() + i, child_coord, &child_state,
					                               max_task_level)
					    .fork();
			}
			co_await lf::join();
		}

		p_editor->JoinNode(coord, p_state, child_states);

		bool changed = false;
		for (Word i = 0; i < 8; ++i) {
			NodePointer<Word> child_ptr{children[i]};
			const NodePointer<Word> &new_child_ptr = new_children[i];

			changed |= new_child_ptr != child_ptr;
			children[i] = *new_child_ptr;
			child_mask ^= (Word{bool(new_child_ptr) != bool(child_ptr)} << i); // Flip if occurrence changed
		}

		if (changed)
			*p_node_ptr = child_mask
			                  ? get_node_pool().template upsert_inner_node<true>(
			                        coord.level, get_node_pool().get_packed_node_inplace(unpacked_node), node_ptr)
			                  : NodePointer<Word>::Null();
		co_return;
	}

public:
	inline NodePoolThreadedEdit() { static_assert(std::is_base_of_v<NodePoolBase<Derived, Word>, Derived>); }

	template <Editor<Word> Editor_T>
	inline NodePointer<Word> ThreadedEdit(lf::busy_pool *p_lf_pool, NodePointer<Word> root_ptr, const Editor_T &editor,
	                                      Word max_task_level = -1) {
		get_node_pool().make_filled_node_pointers();

		return get_node_pool().template edit_switch<Editor_T>(
		    editor, root_ptr, {}, (const typename Editor_T::NodeState *)nullptr,
		    [&](typename Editor_T::NodeState &&state, NodePointer<Word> new_root_ptr) { return new_root_ptr; },
		    [&](typename Editor_T::NodeState &&state) {
			    p_lf_pool->schedule(lf_edit_node<lf::busy_pool::context>(&editor, &root_ptr, NodeCoord<Word>{}, &state,
			                                                             max_task_level));
			    return root_ptr;
		    });
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLTHREADEDEDIT_HPP

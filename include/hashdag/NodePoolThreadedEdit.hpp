//
// Created by adamyuan on 1/17/24.
//

// Threaded NodePool Edit with libfork

#pragma once
#ifndef VKHASHDAG_NODEPOOLTHREADEDEDIT_HPP
#define VKHASHDAG_NODEPOOLTHREADEDEDIT_HPP

#include "NodePool.hpp"

#include <libfork/core.hpp>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word> class NodePoolThreadedEdit {
private:
	inline const NodePoolBase<Derived, Word> &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline NodePoolBase<Derived, Word> &get_node_pool() {
		return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this));
	}

	inline static constexpr auto kEditNodeLF =
	    []<Editor<Word> Editor_T>(auto lf_edit_node, NodePoolThreadedEdit *p_self, const Editor_T &editor,
	                              NodePointer<Word> *p_node_ptr, NodeCoord<Word> coord,
	                              typename Editor_T::NodeState *p_state, Word max_task_level) -> lf::task<> {
		auto &node_pool = p_self->get_node_pool();

		NodePointer<Word> node_ptr = *p_node_ptr;
		if (coord.level == node_pool.m_config.GetNodeLevels() - 1) {
			*p_node_ptr = node_pool.template edit_leaf<true>(editor, node_ptr, coord, *p_state);
			co_return;
		}
		if (coord.level >= max_task_level) {
			*p_node_ptr = node_pool.template edit_node<true>(editor, node_ptr, coord, *p_state);
			co_return;
		}

		std::array<Word, 9> unpacked_node = node_pool.get_unpacked_node_array(node_ptr);

		Word &child_mask = unpacked_node[0];
		std::span<Word, 8> children = std::span<Word, 9>{unpacked_node}.template subspan<1>();

		std::array<NodePointer<Word>, 8> new_children;
		std::array<typename Editor_T::NodeState, 8> child_states{};

		Word fork_count = 0;
		std::array<Word, 8> fork_indices;

		for (Word i = 0; i < 8; ++i) {
			NodePointer<Word> child_ptr{children[i]};
			NodeCoord<Word> child_coord = coord.GetChildCoord(i);
			auto &child_state = child_states[i];
			node_pool.edit_switch(
			    editor, child_ptr, child_coord, child_state, *p_state,
			    [&](NodePointer<Word> new_child_ptr) { new_children[i] = new_child_ptr; },
			    [&]() { fork_indices[fork_count++] = i; });
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
					co_await lf::call[lf_edit_node](p_self, editor, new_children.data() + i, child_coord, &child_state,
					                                max_task_level);
				else
					co_await lf::fork[lf_edit_node](p_self, editor, new_children.data() + i, child_coord, &child_state,
					                                max_task_level);
			}
			co_await lf::join;
		}

		editor.JoinNode(node_pool.m_config, coord, *p_state, child_states);

		bool changed = false;
		for (Word i = 0; i < 8; ++i) {
			NodePointer<Word> child_ptr{children[i]};
			const NodePointer<Word> &new_child_ptr = new_children[i];

			changed |= new_child_ptr != child_ptr;
			children[i] = *new_child_ptr;
			child_mask ^= (Word{bool(new_child_ptr) != bool(child_ptr)} << i); // Flip if occurrence changed
		}

		if (changed)
			*p_node_ptr = child_mask ? node_pool.template upsert_inner_node<true>(
			                               coord.level, node_pool.get_packed_node_inplace(unpacked_node), node_ptr)
			                         : NodePointer<Word>::Null();
		co_return;
	};

public:
	inline NodePoolThreadedEdit() { static_assert(std::is_base_of_v<NodePoolBase<Derived, Word>, Derived>); }

	template <Editor<Word> Editor_T, lf::scheduler Scheduler>
	inline auto ThreadedEdit(Scheduler &&scheduler, NodePointer<Word> root_ptr, const Editor_T &editor,
	                         Word max_task_level,
	                         std::invocable<NodePointer<Word>, typename Editor_T::NodeState> auto &&on_edit_done) {
		get_node_pool().make_filled_node_pointers();

		typename Editor_T::NodeState state{}, parent_state{};
		root_ptr = get_node_pool().template edit_switch<Editor_T>(
		    editor, root_ptr, {}, state, parent_state, [&](NodePointer<Word> new_root_ptr) { return new_root_ptr; },
		    [&]() {
			    lf::sync_wait(std::forward<Scheduler>(scheduler), kEditNodeLF, this, editor, &root_ptr,
			                  NodeCoord<Word>{}, &state, max_task_level);
			    return root_ptr;
		    });
		return on_edit_done(root_ptr, std::move(state));
	}

	template <Editor<Word> Editor_T, lf::scheduler Scheduler>
	inline NodePointer<Word> ThreadedEdit(Scheduler &&scheduler, NodePointer<Word> root_ptr, const Editor_T &editor,
	                                      Word max_task_level = -1) {
		return ThreadedEdit(std::forward<Scheduler>(scheduler), root_ptr, editor, max_task_level,
		                    [](NodePointer<Word> root_ptr, auto &&) { return root_ptr; });
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLTHREADEDEDIT_HPP

//
// Created by adamyuan on 1/17/24.
//

// Threaded NodePool Edit and Iterate with libfork

#pragma once
#ifndef VKHASHDAG_NODEPOOLLIBFORK_HPP
#define VKHASHDAG_NODEPOOLLIBFORK_HPP

#include "Hasher.hpp"
#include "NodePool.hpp"

#include <libfork/task.hpp>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word, Hasher<Word> WordSpanHasher> class NodePoolLibFork {
private:
	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word, WordSpanHasher> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() {
		return *static_cast<NodePoolBase<Derived, Word, WordSpanHasher> *>(static_cast<Derived *>(this));
	}

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_edit_inner_node(Editor<Word> auto &editor, NodePointer<Word> *p_node_ptr,
	                                                        NodeCoord<Word> coord) {
		EditType edit_type = editor.EditNode(coord);
		if (edit_type == EditType::kNotAffected)
			co_return;
		if (edit_type == EditType::kClear) {
			*p_node_ptr = NodePointer<Word>::Null();
			co_return;
		}
		if (edit_type == EditType::kFill) {
			*p_node_ptr = get_node_pool().m_filled_node_pointers[coord.level];
			co_return;
		}

		NodePointer<Word> node_ptr = *p_node_ptr;
		if (coord.level == get_node_pool().m_config.GetNodeLevels() - 1) {
			*p_node_ptr = get_node_pool().template edit_leaf<true>(editor, node_ptr, coord);
			co_return;
		}
		if (coord.level > 10) {
			*p_node_ptr = get_node_pool().template edit_inner_node<true>(editor, node_ptr, coord);
			co_return;
		}

		std::array<Word, 9> unpacked_node = get_node_pool().get_unpacked_node_array(node_ptr);

		Word &child_mask = unpacked_node[0];
		std::span<Word, 8> children = std::span<Word, 9>{unpacked_node}.template subspan<1>();

		std::array<NodePointer<Word>, 8> new_children;
		for (Word i = 0; i < 8; ++i)
			new_children[i] = children[i];

		for (Word i = 0; i < 7; ++i)
			co_await lf_edit_inner_node<Context>(editor, new_children.data() + i, coord.GetChildCoord(i)).fork();
		co_await lf_edit_inner_node<Context>(editor, new_children.data() + 7, coord.GetChildCoord(7));

		co_await lf::join();

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
	inline NodePoolLibFork() { static_assert(std::is_base_of_v<NodePoolBase<Derived, Word, WordSpanHasher>, Derived>); }

	template <typename LibForkPool>
	inline NodePointer<Word> EditLibFork(LibForkPool *p_lf_pool, NodePointer<Word> root_ptr,
	                                     const Editor<Word> auto &editor) {
		get_node_pool().make_filled_node_pointers();
		p_lf_pool->schedule(lf_edit_inner_node<typename LibForkPool::context>(editor, &root_ptr, NodeCoord<Word>{}));
		return root_ptr;
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLLIBFORK_HPP

//
// Created by adamyuan on 1/17/24.
//

// Threaded NodePool Edit and Iterate with libfork

#pragma once
#ifndef VKHASHDAG_NODEPOOLLIBFORK_HPP
#define VKHASHDAG_NODEPOOLLIBFORK_HPP

#include "Hasher.hpp"
#include "NodePool.hpp"

#include <libfork/schedule/busy_pool.hpp>
#include <libfork/task.hpp>

namespace hashdag {

// TODO: Bugfix

template <typename Derived, std::unsigned_integral Word, Hasher<Word> WordSpanHasher> class NodePoolLibFork {
private:
	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word, WordSpanHasher> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() {
		return *static_cast<NodePoolBase<Derived, Word, WordSpanHasher> *>(static_cast<Derived *>(this));
	}

	template <lf::context Context>
	lf::basic_task<void, Context> lf_edit_inner_node(const Editor<Word> auto *p_editor, NodePointer<Word> *p_node_ptr,
	                                                 NodeCoord<Word> coord) {
		NodePointer<Word> node_ptr = *p_node_ptr;
		if (coord.level == get_node_pool().m_config.GetNodeLevels() - 1) {
			*p_node_ptr = get_node_pool().template edit_leaf<true>(*p_editor, node_ptr, coord);
			co_return;
		}
		/* if (coord.level > 10) {
		    *p_node_ptr = get_node_pool().template edit_inner_node<true>(*p_editor, node_ptr, coord);
		    co_return;
		} */

		std::array<Word, 9> unpacked_node = get_node_pool().get_unpacked_node_array(node_ptr);

		Word &child_mask = unpacked_node[0];
		std::span<Word, 8> children = std::span<Word, 9>{unpacked_node}.template subspan<1>();

		auto new_children = std::make_unique_for_overwrite<NodePointer<Word>[]>(8);
		for (Word i = 0; i < 8; ++i)
			new_children[i] = children[i];

#define CHILD(I, POST) \
	do { \
		NodeCoord<Word> child_coord = coord.GetChildCoord(I); \
		EditType child_edit_type = p_editor->EditNode(child_coord); \
		if (child_edit_type == EditType::kClear) \
			new_children[I] = NodePointer<Word>::Null(); \
		else if (child_edit_type == EditType::kFill) \
			new_children[I] = get_node_pool().m_filled_node_pointers[child_coord.level]; \
		else if (child_edit_type == EditType::kAffected) \
			co_await lf_edit_inner_node<Context>(p_editor, new_children.get() + I, child_coord) POST; \
	} while (0)

		CHILD(0, .fork());
		CHILD(1, .fork());
		CHILD(2, .fork());
		CHILD(3, .fork());
		CHILD(4, .fork());
		CHILD(5, .fork());
		CHILD(6, .fork());
		CHILD(7, );
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

	inline NodePointer<Word> EditLibFork(lf::busy_pool *p_lf_pool, NodePointer<Word> root_ptr,
	                                     const Editor<Word> auto &editor) {
		get_node_pool().make_filled_node_pointers();

		EditType edit_type = editor.EditNode({});
		if (edit_type == EditType::kNotAffected)
			return root_ptr;
		if (edit_type == EditType::kClear)
			return NodePointer<Word>::Null();
		if (edit_type == EditType::kFill)
			return get_node_pool().m_filled_node_pointers[0];
		p_lf_pool->schedule(lf_edit_inner_node<lf::busy_pool::context>(&editor, &root_ptr, NodeCoord<Word>{}));
		return root_ptr;
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLLIBFORK_HPP

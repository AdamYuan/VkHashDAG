//
// Created by adamyuan on 1/17/24.
//

// Threaded NodePool Edit with libfork

#pragma once
#ifndef VKHASHDAG_NODEPOOLLIBFORKEDIT_HPP
#define VKHASHDAG_NODEPOOLLIBFORKEDIT_HPP

#include "NodePool.hpp"

#include <libfork/schedule/busy_pool.hpp>
#include <libfork/task.hpp>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word> class NodePoolLibForkEdit {
private:
	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() { return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this)); }

	template <lf::context Context>
	inline lf::basic_task<void, Context> lf_edit_node(const Editor<Word> auto *p_editor, NodePointer<Word> *p_node_ptr,
	                                                  NodeCoord<Word> coord, Word max_task_level) {
		NodePointer<Word> node_ptr = *p_node_ptr;
		if (coord.level == get_node_pool().m_config.GetNodeLevels() - 1) {
			*p_node_ptr = get_node_pool().template edit_leaf<true>(*p_editor, node_ptr, coord);
			co_return;
		}
		if (coord.level >= max_task_level) {
			*p_node_ptr = get_node_pool().template edit_node<true>(*p_editor, node_ptr, coord);
			co_return;
		}

		std::array<Word, 9> unpacked_node = get_node_pool().get_unpacked_node_array(node_ptr);

		Word &child_mask = unpacked_node[0];
		std::span<Word, 8> children = std::span<Word, 9>{unpacked_node}.template subspan<1>();

		std::array<NodePointer<Word>, 8> new_children;
		for (Word i = 0; i < 8; ++i)
			new_children[i] = children[i];

#define CHILD(I, POST) \
	do { \
		NodeCoord<Word> child_coord = coord.GetChildCoord(I); \
		EditType child_edit_type = p_editor->EditNode(child_coord, new_children[I]); \
		if (child_edit_type == EditType::kClear) \
			new_children[I] = NodePointer<Word>::Null(); \
		else if (child_edit_type == EditType::kFill) \
			new_children[I] = get_node_pool().m_filled_node_pointers[child_coord.level]; \
		else if (child_edit_type == EditType::kProceed) \
			co_await lf_edit_node<Context>(p_editor, new_children.data() + I, child_coord, max_task_level) POST; \
	} while (0)

		CHILD(0, .fork());
		CHILD(1, .fork());
		CHILD(2, .fork());
		CHILD(3, .fork());
		CHILD(4, .fork());
		CHILD(5, .fork());
		CHILD(6, .fork());
		CHILD(7, );

#undef CHILD
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
	/* template <lf::context Context>
	inline lf::basic_task<void, Context> lf_iterate_node(Iterator<Word> auto *p_iterator, NodePointer<Word> node_ptr,
	                                                     NodeCoord<Word> coord, Word max_task_level) const {
	    if (coord.level == get_node_pool().m_config.GetNodeLevels() - 1) {
	        get_node_pool().iterate_leaf(p_iterator, node_ptr, coord);
	        co_return;
	    }
	    if (coord.level >= max_task_level) {
	        get_node_pool().iterate_node(p_iterator, node_ptr, coord);
	        co_return;
	    }

	    Word child_mask = 0;
	    const Word *p_next_child = nullptr;

	    if (node_ptr) {
	        const Word *p_node = get_node_pool().read_node(*node_ptr);
	        child_mask = *p_node;
	        p_next_child = p_node + 1;
	    }

#define CHILD(I, POST) \
	do { \
	    NodeCoord<Word> child_coord = coord.GetChildCoord(I); \
	    NodePointer<Word> child_ptr = \
	        ((child_mask >> I) & 1u) ? NodePointer<Word>{*(p_next_child++)} : NodePointer<Word>::Null(); \
	    if (p_iterator->IterateNode(child_coord, child_ptr) != IterateType::kStop) \
	        co_await lf_iterate_node<Context>(p_iterator, child_ptr, child_coord, max_task_level) POST; \
	} while (0)

	    CHILD(0, .fork());
	    CHILD(1, .fork());
	    CHILD(2, .fork());
	    CHILD(3, .fork());
	    CHILD(4, .fork());
	    CHILD(5, .fork());
	    CHILD(6, .fork());
	    CHILD(7, );

#undef CHILD
	    co_await lf::join();
	} */

public:
	inline NodePoolLibForkEdit() { static_assert(std::is_base_of_v<NodePoolBase<Derived, Word>, Derived>); }

	inline NodePointer<Word> LibForkEdit(lf::busy_pool *p_lf_pool, NodePointer<Word> root_ptr,
	                                     const Editor<Word> auto &editor, Word max_task_level = -1) {
		get_node_pool().make_filled_node_pointers();

		EditType edit_type = editor.EditNode({}, root_ptr);
		if (edit_type == EditType::kNotAffected)
			return root_ptr;
		if (edit_type == EditType::kClear)
			return NodePointer<Word>::Null();
		if (edit_type == EditType::kFill)
			return get_node_pool().m_filled_node_pointers[0];

		p_lf_pool->schedule(
		    lf_edit_node<lf::busy_pool::context>(&editor, &root_ptr, NodeCoord<Word>{}, max_task_level));
		return root_ptr;
	}

	/* inline void IterateLibFork(lf::busy_pool *p_lf_pool, NodePointer<Word> root_ptr, Iterator<Word> auto *p_iterator,
	                           Word max_task_level = -1) {
	    get_node_pool().make_filled_node_pointers();

	    if (p_iterator->IterateNode({}, root_ptr) == IterateType::kStop)
	        return;
	    p_lf_pool->schedule(
	        lf_iterate_node<lf::busy_pool::context>(p_iterator, root_ptr, NodeCoord<Word>{}, max_task_level));
	} */
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLLIBFORKEDIT_HPP

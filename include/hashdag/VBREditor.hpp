//
// Created by adamyuan on 3/27/24.
//

#pragma once
#ifndef VKHASHDAG_VBREDITOR_HPP
#define VKHASHDAG_VBREDITOR_HPP

#include "Editor.hpp"
#include "VBROctree.hpp"

namespace hashdag {

struct VBREditType {
	EditType type;
	VBRColor fill_color;
};

template <typename T, typename Word>
concept VBREditor = requires(const T ce) {
	{ ce.EditNode(NodeCoord<Word>{}, NodePointer<Word>{}) } -> std::convertible_to<VBREditType>;
	{ ce.EditVoxel(NodeCoord<Word>{}, bool{}, (VBRColor *){}) } -> std::convertible_to<bool>;
} && std::unsigned_integral<Word>;

template <std::unsigned_integral Word, VBROctree<Word> Octree_T, VBREditor<Word> Editor_T> struct VBREditorWrapper {
	struct NodeState {
		NodePointer<Word> octree_node_ptr;
		std::variant<std::monostate, VBRColorBlockWriter, VBRColorBlockWriter *> writer_variant;
	};

	Editor_T editor;
	Octree_T octree;
	NodePointer<Word> octree_root_ptr;

	inline EditType EditNode(const NodeCoord<Word> &coord, NodePointer<Word> node_ptr, NodeState *p_state,
	                         const NodeState *p_parent_state) const {
		auto [edit_type, color] = editor.EditNode(coord, node_ptr);

		// TODO: Process Fill
		if (coord.level < octree.GetBlockLevel()) {
			p_state->octree_node_ptr = coord.level == 0
			                               ? octree_root_ptr
			                               : octree.GetNode(p_parent_state->octree_node_ptr, coord.GetChildIndex());
			if (coord.level == octree.GetBlockLevel()) {
				p_state->writer_variant =
				    VBRColorBlockWriter{VBRColorBlock::Iterator{octree.GetBlock(p_state->octree_node_ptr)}};
			}
		} else {
			VBRColorBlockWriter *p_writer = coord.level == octree.GetBlockLevel() + 1
			                                    ? &std::get<VBRColorBlockWriter>(p_parent_state->writer_variant)
			                                    : std::get<VBRColorBlockWriter *>(p_parent_state->writer_variant);
			p_state->writer_variant = p_writer;
		}

		return edit_type;
	}
	inline bool EditVoxel(const NodeCoord<Word> &coord, bool voxel, const NodeState *p_state) const {
		VBRColorBlockWriter *p_writer = coord.level == octree.GetBlockLevel() + 1
		                                    ? &std::get<VBRColorBlockWriter>(p_state->writer_variant)
		                                    : std::get<VBRColorBlockWriter>(p_state->writer_variant);
		p_writer->Edit([&](VBRColor *p_color) { voxel = editor.EditVoxel(coord, voxel, p_color); });
		return voxel;
	}

	inline void JoinNode(const NodeCoord<Word> &coord, NodeState *p_state, std::span<const NodeState, 8> child_states) {
		if (coord.level == octree.GetBlockLevel()) {
			p_state->octree_node_ptr = octree.SetBlock(p_state->octree_node_ptr,
			                                           std::get<VBRColorBlockWriter>(p_state->writer_variant).Flush());
		} else if (coord.level < octree.GetBlockLevel()) {
			std::array<NodePointer<Word>, 8> child_ptrs;
			for (Word i = 0; i < 8; ++i)
				child_ptrs[i] = child_states[i].octree_node_ptr;
			p_state->octree_node_ptr = octree.SetNode(p_state->octree_node_ptr, child_ptrs);
		}
	}
	inline void JoinLeaf(const NodeCoord<Word> &coord, NodeState *p_state) {
		if (coord.level == octree.GetBlockLevel()) {
			p_state->octree_node_ptr = octree.SetBlock(p_state->octree_node_ptr,
			                                           std::get<VBRColorBlockWriter>(p_state->writer_variant).Flush());
		}
	}
};

} // namespace hashdag

#endif // VKHASHDAG_VBREDITOR_HPP

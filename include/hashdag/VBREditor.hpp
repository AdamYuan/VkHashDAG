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
	struct VBRModifier {
		VBRColorBlockWriter writer;
	};
	struct NodeState {
		NodePointer<Word> octree_node_ptr;
		std::variant<std::monostate, VBRModifier, VBRModifier *> modifier_variant;
	};

	Editor_T editor;
	Octree_T octree;
	NodePointer<Word> octree_root_ptr;

	inline EditType EditNode(const NodeCoord<Word> &coord, NodePointer<Word> node_ptr, NodeState *p_state,
	                         const NodeState *p_parent_state) const {
		auto [edit_type, color] = editor.EditNode(coord, node_ptr);

		// TODO: Process Fill
		if (coord.level <= octree.GetBlockLevel()) {
			p_state->octree_node_ptr = coord.level == 0
			                               ? octree_root_ptr
			                               : octree.GetNode(p_parent_state->octree_node_ptr, coord.GetChildIndex());
			if (coord.level == octree.GetBlockLevel()) {
				p_state->modifier_variant = VBRModifier{
				    .writer = VBRColorBlockWriter{octree.GetBlock(p_state->octree_node_ptr), 114514u},
				};
			}
		} else {
			VBRModifier *p_modifier = coord.level == octree.GetBlockLevel() + 1
			                              ? &std::get<VBRModifier>(p_parent_state->modifier_variant)
			                              : std::get<VBRModifier>(p_parent_state->modifier_variant);
			p_state->modifier_variant = p_modifier;
		}

		return edit_type;
	}
	inline bool EditVoxel(const NodeCoord<Word> &coord, bool voxel, const NodeState *p_state) const {
		VBRModifier *p_modifier = coord.level == octree.GetBlockLevel() + 1
		                              ? &std::get<VBRModifier>(p_state->modifier_variant)
		                              : std::get<VBRModifier>(p_state->modifier_variant);
		VBRColor color = {}; // TODO: Read
		voxel = editor.EditVoxel(coord, voxel, &color);
		p_modifier->writer.SetNextColor(coord.GetSubCoord(octree.GetBlockLevel()), color);
		return voxel;
	}

	inline void JoinNode(const NodeCoord<Word> &coord, NodeState *p_state, std::span<const NodeState, 8> child_states) {
	}
	inline void JoinLeaf(const NodeCoord<Word> &coord, NodeState *p_state) {}
};

} // namespace hashdag

#endif // VKHASHDAG_VBREDITOR_HPP

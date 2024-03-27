//
// Created by adamyuan on 3/27/24.
//

#pragma once
#ifndef VKHASHDAG_VBREDITOR_HPP
#define VKHASHDAG_VBREDITOR_HPP

#include "Editor.hpp"
#include "VBROctree.hpp"

namespace hashdag {

template <typename T, typename Word>
concept VBREditor = requires(const T ce) {
	{ ce.EditNode(NodeCoord<Word>{}, NodePointer<Word>{}) } -> std::convertible_to<std::tuple<EditType, RGBColor>>;
	{ ce.EditVoxel(NodeCoord<Word>{}, bool{}, RGBColor{}) } -> std::convertible_to<std::tuple<bool, RGBColor>>;
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

	inline std::tuple<EditType, NodeState> EditNode(const NodeCoord<Word> &coord, NodePointer<Word> node_ptr,
	                                                const NodeState *p_parent_state) const {
		auto [edit_type, color] = editor.EditNode(coord, node_ptr);
		NodeState state{};

		// TODO: Process Fill
		if (coord.level <= octree.GetBlockLevel()) {
			state.octree_node_ptr = coord.level == 0
			                            ? octree_root_ptr
			                            : octree.GetNode(p_parent_state->octree_node_ptr, coord.GetChildIndex());
			if (coord.level == octree.GetBlockLevel()) {
				state.modifier_variant = VBRModifier{
				    .writer = VBRColorBlockWriter{octree.GetBlock(state.octree_node_ptr), 114514u},
				};
			}
		} else {
			VBRModifier *p_modifier = coord.level == octree.GetBlockLevel() + 1
			                              ? &std::get<VBRModifier>(p_parent_state->modifier_variant)
			                              : std::get<VBRModifier>(p_parent_state->modifier_variant);
			state.modifier_variant = p_modifier;
		}

		return {edit_type, std::move(state)};
	}
	inline bool EditVoxel(const NodeCoord<Word> &coord, bool voxel, const NodeState *p_state) const {
		VBRModifier *p_modifier = coord.level == octree.GetBlockLevel() + 1
		                              ? &std::get<VBRModifier>(p_state->modifier_variant)
		                              : std::get<VBRModifier>(p_state->modifier_variant);
		auto [new_voxel, new_color] = editor.EditVoxel(coord, voxel, RGBColor{});
		p_modifier->writer.SetNextColor(coord.GetSubCoord(octree.GetBlockLevel()), new_color);
		return new_voxel;
	}

	inline void JoinNode(const NodeCoord<Word> &coord, NodeState *p_state, std::span<const NodeState, 8> child_states) {
	}
	inline void JoinLeaf(const NodeCoord<Word> &coord, NodeState *p_state) {}
};

} // namespace hashdag

#endif // VKHASHDAG_VBREDITOR_HPP

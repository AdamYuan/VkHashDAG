//
// Created by adamyuan on 3/27/24.
//

#pragma once
#ifndef VKHASHDAG_VBREDITOR_HPP
#define VKHASHDAG_VBREDITOR_HPP

#include "Editor.hpp"
#include "VBROctree.hpp"

#include <memory>

namespace hashdag {

struct VBREditType {
	EditType type;
	VBRColor fill_color;
};
struct VBRVoxel {
	bool voxel;
	VBRColor color;
};

template <typename T, typename Word>
concept VBREditor = requires(const T ce) {
	{ ce.EditNode(Config<Word>{}, NodeCoord<Word>{}, NodePointer<Word>{}) } -> std::convertible_to<VBREditType>;
	{ ce.EditVoxel(Config<Word>{}, NodeCoord<Word>{}, VBRVoxel{}) } -> std::convertible_to<VBRVoxel>;
} && std::unsigned_integral<Word>;

template <std::unsigned_integral Word, VBREditor<Word> Editor_T, VBROctree<Word> Octree_T> struct VBREditorWrapper {
	struct NodeState {
		typename Octree_T::Pointer octree_node;
		typename Octree_T::Writer *p_writer;
	};

	Editor_T editor;
	Octree_T *p_octree;
	typename Octree_T::Pointer octree_root;

	inline EditType EditNode(const Config<Word> &config, const NodeCoord<Word> &coord, NodePointer<Word> node_ptr,
	                         NodeState *p_state, const NodeState *p_parent_state) const {
		auto [edit_type, color] = editor.EditNode(config, coord, node_ptr);

		if (coord.level <= p_octree->GetLeafLevel()) {
			p_state->octree_node =
			    coord.level == 0 ? octree_root : p_octree->GetNode(p_parent_state->octree_node, coord.GetChildIndex());

			if (edit_type == EditType::kFill)
				p_state->octree_node = p_octree->FillNode(p_state->octree_node, color);
			else if (edit_type == EditType::kClear)
				p_state->octree_node = p_octree->ClearNode(p_state->octree_node);
			else if (edit_type == EditType::kProceed && coord.level == p_octree->GetLeafLevel())
				p_state->p_writer = new typename Octree_T::Writer(p_octree->WriteLeaf(p_state->octree_node));
		} else {
			auto p_writer = p_parent_state->p_writer;
			p_state->p_writer = p_writer;

			uint32_t voxel_count = 1u << ((config.GetVoxelLevel() - coord.level) * 3u);
			if (edit_type == EditType::kNotAffected)
				p_writer->Copy(voxel_count);
			else if (edit_type != EditType::kProceed)
				p_writer->Push(color, voxel_count);
		}

		return edit_type;
	}
	inline bool EditVoxel(const Config<Word> &config, const NodeCoord<Word> &coord, bool voxel,
	                      const NodeState *p_state) const {
		p_state->p_writer->Edit([&](VBRColor *p_color) {
			VBRVoxel vbr_voxel = editor.EditVoxel(config, coord, VBRVoxel{voxel, *p_color});
			voxel = vbr_voxel.voxel;
			*p_color = vbr_voxel.color;
		});
		return voxel;
	}

	inline void JoinNode(const Config<Word> &, const NodeCoord<Word> &coord, NodeState *p_state,
	                     std::span<const NodeState, 8> child_states) const {
		if (coord.level == p_octree->GetLeafLevel()) {
			p_state->octree_node = p_octree->FlushLeaf(p_state->octree_node, std::move(*p_state->p_writer));
			delete p_state->p_writer;
		} else if (coord.level < p_octree->GetLeafLevel()) {
			std::array<typename Octree_T::Pointer, 8> octree_children = {
			    child_states[0].octree_node, child_states[1].octree_node, child_states[2].octree_node,
			    child_states[3].octree_node, child_states[4].octree_node, child_states[5].octree_node,
			    child_states[6].octree_node, child_states[7].octree_node};
			p_state->octree_node = p_octree->SetNode(p_state->octree_node, octree_children);
		}
	}
	inline void JoinLeaf(const Config<Word> &, const NodeCoord<Word> &coord, NodeState *p_state) const {
		if (coord.level == p_octree->GetLeafLevel()) {
			p_state->octree_node = p_octree->FlushLeaf(p_state->octree_node, std::move(*p_state->p_writer));
			delete p_state->p_writer;
		}
	}
};

} // namespace hashdag

#endif // VKHASHDAG_VBREDITOR_HPP

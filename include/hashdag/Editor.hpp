//
// Created by adamyuan on 1/14/24.
//

#pragma once
#ifndef VKHASHDAG_EDITOR_HPP
#define VKHASHDAG_EDITOR_HPP

#include "NodeCoord.hpp"
#include "NodePointer.hpp"
#include <concepts>
#include <span>
#include <variant>

namespace hashdag {

enum class EditType { kNotAffected, kProceed, kFill, kClear };

template <typename T, typename Word>
concept Editor = requires(const T ce) {
	typename T::NodeState;
	ce.ReduceNode(NodeCoord<Word>{}, typename T::NodeState{},
	              std::declval<std::span<const typename T::NodeState *, 8>>());

	{
		ce.EditNode(NodeCoord<Word>{}, NodePointer<Word>{}, (const typename T::NodeState *){})
	} -> std::convertible_to<std::tuple<EditType, typename T::NodeState>>;
	{ ce.EditVoxel(NodeCoord<Word>{}, bool{}, (const typename T::NodeState *){}) } -> std::convertible_to<bool>;
};

template <typename T, typename Word>
concept StatelessEditor = requires(const T ce) {
	{ ce.EditNode(NodeCoord<Word>{}, NodePointer<Word>{}) } -> std::convertible_to<EditType>;
	{ ce.EditVoxel(NodeCoord<Word>{}, bool{}) } -> std::convertible_to<bool>;
};

template <std::unsigned_integral Word, StatelessEditor<Word> Editor_T> struct StatelessEditorWrapper {
	Editor_T editor;

	using NodeState = std::monostate;
	inline static void ReduceNode(auto &&, auto &&, auto &&) {}
	inline std::tuple<EditType, NodeState> EditNode(const NodeCoord<Word> &coord, NodePointer<Word> node_ptr,
	                                                auto &&) const {
		return {editor.EditNode(coord, node_ptr), NodeState{}};
	}
	inline bool EditVoxel(const NodeCoord<Word> &coord, bool voxel, auto &&) const {
		return editor.EditVoxel(coord, voxel);
	}
};

} // namespace hashdag

#endif // VKHASHDAG_EDITOR_HPP

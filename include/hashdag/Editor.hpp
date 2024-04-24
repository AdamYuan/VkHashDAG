//
// Created by adamyuan on 1/14/24.
//

#pragma once
#ifndef VKHASHDAG_EDITOR_HPP
#define VKHASHDAG_EDITOR_HPP

#include "Config.hpp"
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
	ce.JoinNode(Config<Word>{}, NodeCoord<Word>{}, std::declval<typename T::NodeState &>(),
	            std::declval<std::span<const typename T::NodeState, 8>>());
	ce.JoinLeaf(Config<Word>{}, NodeCoord<Word>{}, std::declval<typename T::NodeState &>());

	{
		ce.EditNode(Config<Word>{}, NodeCoord<Word>{}, NodePointer<Word>{}, std::declval<typename T::NodeState &>(),
		            std::declval<const typename T::NodeState &>())
	} -> std::convertible_to<EditType>;
	{
		ce.EditVoxel(Config<Word>{}, NodeCoord<Word>{}, bool{}, std::declval<typename T::NodeState &>())
	} -> std::convertible_to<bool>;
} && std::unsigned_integral<Word>;

template <typename T, typename Word>
concept StatelessEditor = requires(const T ce) {
	{ ce.EditNode(Config<Word>{}, NodeCoord<Word>{}, NodePointer<Word>{}) } -> std::convertible_to<EditType>;
	{ ce.EditVoxel(Config<Word>{}, NodeCoord<Word>{}, bool{}) } -> std::convertible_to<bool>;
} && std::unsigned_integral<Word>;

template <std::unsigned_integral Word, StatelessEditor<Word> Editor_T> struct StatelessEditorWrapper {
	Editor_T editor;

	using NodeState = std::monostate;
	inline EditType EditNode(const Config<Word> &config, const NodeCoord<Word> &coord, NodePointer<Word> node_ptr,
	                         auto &&, auto &&) const {
		return editor.EditNode(config, coord, node_ptr);
	}
	inline bool EditVoxel(const Config<Word> &config, const NodeCoord<Word> &coord, bool voxel, auto &&) const {
		return editor.EditVoxel(config, coord, voxel);
	}
	inline static void JoinNode(auto &&, auto &&, auto &&, auto &&) {}
	inline static void JoinLeaf(auto &&, auto &&, auto &&) {}
};

} // namespace hashdag

#endif // VKHASHDAG_EDITOR_HPP

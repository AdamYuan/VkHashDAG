//
// Created by adamyuan on 1/14/24.
//

#pragma once
#ifndef VKHASHDAG_EDITOR_HPP
#define VKHASHDAG_EDITOR_HPP

#include "NodeCoord.hpp"
#include <concepts>

namespace hashdag {

enum class EditType { kNotAffected, kAffected, kFill, kClear };

template <typename T, typename Word>
concept Editor = requires(const T ce) {
	{ ce.EditNode(NodeCoord<Word>{}) } -> std::convertible_to<EditType>;
	{ ce.EditVoxel(NodeCoord<Word>{}, bool{}) } -> std::convertible_to<bool>;
};

struct FillEditor {
	inline static EditType EditNode(auto &&) { return EditType::kFill; }
	inline static bool EditVoxel(auto &&, auto &&) { return true; }
};

template <typename T, typename Word>
concept ThreadedEditor = Editor<T, Word> && requires(const T ce) {
	{ ce.GetAffectedExtent(NodeCoord<Word>{}) } -> std::convertible_to<uint64_t>;
	{ ce.GetJobLowestLevel() } -> std::convertible_to<Word>;
};

} // namespace hashdag

#endif // VKHASHDAG_EDITOR_HPP

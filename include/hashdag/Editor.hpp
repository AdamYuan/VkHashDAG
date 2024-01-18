//
// Created by adamyuan on 1/14/24.
//

#pragma once
#ifndef VKHASHDAG_EDITOR_HPP
#define VKHASHDAG_EDITOR_HPP

#include "NodeCoord.hpp"
#include "NodePointer.hpp"
#include <concepts>

namespace hashdag {

enum class EditType { kNotAffected, kProceed, kFill, kClear };

template <typename T, typename Word>
concept Editor = requires(const T ce) {
	{ ce.EditNode(NodeCoord<Word>{}, NodePointer<Word>{}) } -> std::convertible_to<EditType>;
	{ ce.EditVoxel(NodeCoord<Word>{}, bool{}) } -> std::convertible_to<bool>;
};

} // namespace hashdag

#endif // VKHASHDAG_EDITOR_HPP

//
// Created by adamyuan on 1/14/24.
//

#pragma once
#ifndef VKHASHDAG_EDITOR_HPP
#define VKHASHDAG_EDITOR_HPP

#include "Position.hpp"
#include <concepts>

namespace hashdag {

template <typename T, typename Word>
concept Editor = requires(T e) {
	{ e.IsAffected(NodeCoord<Word>{}) } -> std::convertible_to<bool>;
	{ e.Edit(NodeCoord<Word>{}, bool{}) } -> std::convertible_to<bool>;
};

} // namespace hashdag

#endif // VKHASHDAG_EDITOR_HPP

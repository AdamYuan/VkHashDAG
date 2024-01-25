//
// Created by adamyuan on 1/19/24.
//

#pragma once
#ifndef VKHASHDAG_COLORSVO_COLORBLOCK_HPP
#define VKHASHDAG_COLORSVO_COLORBLOCK_HPP

#include <concepts>

#include "Color.hpp"
#include "NodeCoord.hpp"

namespace hashdag {

template <typename T, typename Word, typename Color>
concept ColorBlock = requires(T c, const T cc) {
	{ cc.GetColor(NodeCoord<Word>{}) } -> std::convertible_to<Color>;
	c.SetColor(NodeCoord<Word>{}, Color{});
};

} // namespace hashdag

#endif // VKHASHDAG_COLORSVO_COLORBLOCK_HPP

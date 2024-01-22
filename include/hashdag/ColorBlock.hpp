//
// Created by adamyuan on 1/19/24.
//

#pragma once
#ifndef VKHASHDAG_COLORSVO_COLORBLOCK_HPP
#define VKHASHDAG_COLORSVO_COLORBLOCK_HPP

#include <concepts>
#include <glm/glm.hpp>

namespace hashdag {

template <typename T, typename Word, typename Color>
concept ColorBlock = requires(T c, const T cc) {
	{ cc.GetColor(glm::vec<3, Word>{}) } -> std::convertible_to<Color>;
	c.SetColor(glm::vec<3, Word>{}, Color{});
};

} // namespace colorsvo

#endif // VKHASHDAG_COLORSVO_COLORBLOCK_HPP

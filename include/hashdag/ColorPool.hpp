//
// Created by adamyuan on 1/19/24.
//

#pragma once
#ifndef VKHASHDAG_HASHDAG_COLORPOOL_HPP
#define VKHASHDAG_HASHDAG_COLORPOOL_HPP

#include "NodeCoord.hpp"

namespace hashdag {

template <typename T, typename Word, typename ColorBlock>
concept ColorPool = requires(T p, const T cp) {
	{ p.GetBlock(NodeCoord<Word>{}) } -> std::convertible_to<ColorBlock &>;
	{ cp.GetBlock(NodeCoord<Word>{}) } -> std::convertible_to<ColorBlock>;
	p.AllocateBlock(NodeCoord<Word>{});
};

} // namespace hashdag

#endif // VKHASHDAG_HASHDAG_COLORPOOL_HPP

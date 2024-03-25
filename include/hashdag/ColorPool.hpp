//
// Created by adamyuan on 1/19/24.
//

#pragma once
#ifndef VKHASHDAG_HASHDAG_COLORPOOL_HPP
#define VKHASHDAG_HASHDAG_COLORPOOL_HPP

#include "NodeCoord.hpp"

namespace hashdag {

template <typename T, typename Word, typename ColorBlock, typename ColorBlockWriter>
concept ColorPool = requires(T p, const T cp) {
	{ cp.ReadBlock(NodeCoord<Word>{}) } -> std::convertible_to<const ColorBlock *>;
	{ p.WriteBlock(NodeCoord<Word>{}) } -> std::convertible_to<ColorBlockWriter>;
	p.FreeBlock(NodeCoord<Word>{});
};

} // namespace hashdag

#endif // VKHASHDAG_HASHDAG_COLORPOOL_HPP

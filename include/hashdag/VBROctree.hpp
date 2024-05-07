//
// Created by adamyuan on 3/26/24.
//

#pragma once
#ifndef VKHASHDAG_VBROCTREE_HPP
#define VKHASHDAG_VBROCTREE_HPP

#include "VBRColor.hpp"
#include <span>

namespace hashdag {

template <typename T> using VBROctreePointer = std::decay_t<decltype(std::declval<T>().GetNode({}, 0))>;
template <typename T> using VBROctreeLeaf = std::decay_t<decltype(std::declval<T>().GetLeaf({}))>;
template <typename T>
using VBROctreeLeafWriter = std::decay_t<decltype(VBRChunkWriter{std::declval<VBROctreeLeaf<T>>()})>;

template <typename T, typename Word>
concept VBROctree = requires(T e, const T ce, VBROctreePointer<T> pointer, VBROctreeLeaf<T> leaf) {
	{ ce.GetNode(pointer, 0) } -> std::convertible_to<decltype(pointer)>;
	{
		e.SetNode(pointer, std::declval<std::span<const decltype(pointer), 8>>())
	} -> std::convertible_to<decltype(pointer)>;
	{ e.ClearNode(pointer) } -> std::convertible_to<decltype(pointer)>;
	{ e.FillNode(pointer, VBRColor{}) } -> std::convertible_to<decltype(pointer)>;

	{ ce.GetLeaf(pointer) } -> std::convertible_to<decltype(leaf)>;
	{ e.SetLeaf(pointer, VBRChunk<Word, std::vector>{}) } -> std::convertible_to<decltype(pointer)>;
	{ ce.GetLeafLevel() } -> std::convertible_to<Word>;
};

} // namespace hashdag

#endif

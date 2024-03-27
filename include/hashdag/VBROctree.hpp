//
// Created by adamyuan on 3/26/24.
//

#pragma once
#ifndef VKHASHDAG_VBROCTREE_HPP
#define VKHASHDAG_VBROCTREE_HPP

#include "NodePointer.hpp"
#include "VBRColor.hpp"
#include <span>

namespace hashdag {

template <typename T, typename Word>
concept VBROctree = requires(T e, const T ce) {
	{ ce.GetNode(NodePointer<Word>{}, 0) } -> std::convertible_to<NodePointer<Word>>;
	{
		ce.SetNode(NodePointer<Word>{}, std::declval<std::span<NodePointer<Word>, 8>>())
	} -> std::convertible_to<NodePointer<Word>>;

	{ ce.GetBlock(NodePointer<Word>{}) } -> std::convertible_to<VBRColorBlock *>;
	{ ce.SetBlock(NodePointer<Word>{}, VBRColorBlock{}) } -> std::convertible_to<NodePointer<Word>>;
	{ ce.GetBlockLevel() } -> std::convertible_to<Word>;
} && std::unsigned_integral<Word>;

} // namespace hashdag

#endif

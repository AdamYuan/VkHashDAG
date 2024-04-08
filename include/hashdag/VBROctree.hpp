//
// Created by adamyuan on 3/26/24.
//

#pragma once
#ifndef VKHASHDAG_VBROCTREE_HPP
#define VKHASHDAG_VBROCTREE_HPP

#include "VBRColor.hpp"
#include <span>

namespace hashdag {

template <typename T, typename Pointer>
concept VBROctree = requires(T e, const T ce) {
	{ ce.GetNode(Pointer{}, 0) } -> std::convertible_to<Pointer>;
	{ e.SetNode(Pointer{}, std::declval<std::span<Pointer, 8>>()) } -> std::convertible_to<Pointer>;
	{ e.FillNode(Pointer{}, VBRColor{}) } -> std::convertible_to<Pointer>;

	{ ce.GetBlock(Pointer{}) } -> std::convertible_to<const VBRColorBlock *>;
	{ e.SetBlock(Pointer{}, VBRColorBlock{}) } -> std::convertible_to<Pointer>;
	{ ce.GetBlockLevel() } -> std::convertible_to<Pointer>;
};

} // namespace hashdag

#endif

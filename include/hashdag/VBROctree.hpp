//
// Created by adamyuan on 3/26/24.
//

#pragma once
#ifndef VKHASHDAG_VBROCTREE_HPP
#define VKHASHDAG_VBROCTREE_HPP

#include "VBRColor.hpp"
#include <span>
#include <typeinfo>

namespace hashdag {

template <typename T, typename Word>
concept VBROctree = requires(T e, const T ce, typename T::Pointer pointer, typename T::Writer writer) {
	typename T::Writer;
	typename T::Pointer;

	{ ce.GetNode(pointer, 0) } -> std::convertible_to<typename T::Pointer>;
	{
		e.SetNode(pointer, std::declval<std::span<const typename T::Pointer, 8>>())
	} -> std::convertible_to<typename T::Pointer>;
	{ e.ClearNode(pointer) } -> std::convertible_to<typename T::Pointer>;
	{ e.FillNode(pointer, VBRColor{}) } -> std::convertible_to<typename T::Pointer>;

	{ ce.WriteLeaf(pointer) } -> std::convertible_to<typename T::Writer>;
	{ e.FlushLeaf(pointer, std::move(writer)) } -> std::convertible_to<typename T::Pointer>;
	{ ce.GetLeafLevel() } -> std::convertible_to<Word>;
};

} // namespace hashdag

#endif

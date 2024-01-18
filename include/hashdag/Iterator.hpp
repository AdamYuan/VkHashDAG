//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_ITERATOR_HPP
#define VKHASHDAG_ITERATOR_HPP

#include "NodeCoord.hpp"
#include "NodePointer.hpp"

namespace hashdag {

enum class IterateType { kStop, kProceed };

template <typename T, typename Word>
concept Iterator = requires(T i) {
	{ i.IterateNode(NodeCoord<Word>{}, NodePointer<Word>{}) } -> std::convertible_to<IterateType>;
	i.IterateVoxel(NodeCoord<Word>{}, bool{});
};

} // namespace hashdag

#endif // VKHASHDAG_ITERATOR_HPP

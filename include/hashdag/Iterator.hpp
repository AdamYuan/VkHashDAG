//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_ITERATOR_HPP
#define VKHASHDAG_ITERATOR_HPP

#include "NodeCoord.hpp"

namespace hashdag {

template <typename T, typename Word>
concept Iterator = requires(T e, const T ce) {
	{ ce.IsAffected(NodeCoord<Word>{}) } -> std::convertible_to<bool>;
	e.Iterate(NodeCoord<Word>{});
};

} // namespace hashdag

#endif // VKHASHDAG_ITERATOR_HPP

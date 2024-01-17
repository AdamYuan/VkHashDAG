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

template <typename T, typename Word>
concept ThreadedIterator = Editor<T, Word> && requires(const T ce) {
	{ ce.GetAffectedExtent(NodeCoord<Word>{}) } -> std::convertible_to<uint64_t>;
};

} // namespace hashdag

#endif // VKHASHDAG_ITERATOR_HPP

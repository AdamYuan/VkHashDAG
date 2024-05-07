//
// Created by adamyuan on 5/7/24.
//

#pragma once
#ifndef VKHASHDAG_RANGE_HPP
#define VKHASHDAG_RANGE_HPP

#include <algorithm>
#include <concepts>

template <std::unsigned_integral T> struct Range {
	T begin = std::numeric_limits<T>::max(), end = std::numeric_limits<uint32_t>::min();

	inline void Union(Range r) {
		begin = std::min(begin, r.begin);
		end = std::max(end, r.end);
	}
};

#endif

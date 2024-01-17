//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_NODECOORD_HPP
#define VKHASHDAG_NODECOORD_HPP

#include "Vec3.hpp"
#include <concepts>

namespace hashdag {

template <std::unsigned_integral Word> struct NodeCoord {
	Word level;
	Vec3<Word> pos;

	inline constexpr NodeCoord GetChildCoord(Word child_index) const {
		return {
		    .level = level + 1,
		    .pos = {.x = (pos.x << 1u) | ((child_index >> 0u) & 1u),
		            .y = (pos.y << 1u) | ((child_index >> 1u) & 1u),
		            .z = (pos.z << 1u) | ((child_index >> 2u) & 1u)},
		};
	}

	inline constexpr NodeCoord GetLeafCoord(Word leaf_index) const {
		// return GetChildCoord(leaf_index >> 3u).GetChildCoord(leaf_index & 7u);
		return {
		    .level = level + 2,
		    .pos = {.x = (pos.x << 2u) | ((leaf_index >> 2u) & 2u) | ((leaf_index >> 0u) & 1u),
		            .y = (pos.y << 2u) | ((leaf_index >> 3u) & 2u) | ((leaf_index >> 1u) & 1u),
		            .z = (pos.z << 2u) | ((leaf_index >> 4u) & 2u) | ((leaf_index >> 2u) & 1u)},
		};
	}

	template <std::floating_point Float> inline constexpr Vec3<Float> GetCenter() const {
		Float base = Float(1) / Float(1u << level), offset = Float(0.5) * base;
		return {
		    .x = Float(pos.x) * base + offset,
		    .y = Float(pos.y) * base + offset,
		    .z = Float(pos.z) * base + offset,
		};
	}
	template <std::floating_point Float> inline constexpr Vec3<Float> GetLowerBound() const {
		Float base = Float(1) / Float(1u << level);
		return {
		    .x = Float(pos.x) * base,
		    .y = Float(pos.y) * base,
		    .z = Float(pos.z) * base,
		};
	}
	template <std::floating_point Float> inline constexpr Vec3<Float> GetUpperBound() const {
		Float base = Float(1) / Float(1u << level);
		return {
		    .x = Float(pos.x + 1u) * base,
		    .y = Float(pos.y + 1u) * base,
		    .z = Float(pos.z + 1u) * base,
		};
	}
	inline constexpr Vec3<Word> GetLowerBoundAtLevel(Word at_level) const {
		Word bits = at_level - level;
		return {
		    .x = pos.x << bits,
		    .y = pos.y << bits,
		    .z = pos.z << bits,
		};
	}
	inline constexpr Vec3<Word> GetUpperBoundAtLevel(Word at_level) const {
		Word bits = at_level - level;
		return {
		    .x = (pos.x + 1) << bits,
		    .y = (pos.y + 1) << bits,
		    .z = (pos.z + 1) << bits,
		};
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODECOORD_HPP

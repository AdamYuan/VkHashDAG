//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_NODECOORD_HPP
#define VKHASHDAG_NODECOORD_HPP

#include <concepts>
#include <glm/glm.hpp>

namespace hashdag {

template <std::unsigned_integral Word> struct NodeCoord {
	Word level;
	glm::vec<3, Word> pos;

	inline constexpr NodeCoord GetChildCoord(Word child_index) const {
		return {
		    .level = level + 1,
		    .pos =
		        {
		            (pos.x << 1u) | ((child_index >> 0u) & 1u),
		            (pos.y << 1u) | ((child_index >> 1u) & 1u),
		            (pos.z << 1u) | ((child_index >> 2u) & 1u),
		        },
		};
	}

	inline constexpr NodeCoord GetLeafCoord(Word leaf_index) const {
		// return GetChildCoord(leaf_index >> 3u).GetChildCoord(leaf_index & 7u);
		return {
		    .level = level + 2,
		    .pos =
		        {
		            (pos.x << 2u) | ((leaf_index >> 2u) & 2u) | ((leaf_index >> 0u) & 1u),
		            (pos.y << 2u) | ((leaf_index >> 3u) & 2u) | ((leaf_index >> 1u) & 1u),
		            (pos.z << 2u) | ((leaf_index >> 4u) & 2u) | ((leaf_index >> 2u) & 1u),
		        },
		};
	}

	inline constexpr NodeCoord GetParentCoord(Word parent_level) const {
		Word bits = level - parent_level;
		return {
		    .level = parent_level,
		    .pos = pos >> bits,
		};
	}

	inline constexpr NodeCoord GetSubCoord(Word parent_level) const {
		Word bits = level - parent_level;
		Word mask = (Word(1) << bits) - Word(1);
		return {
		    .level = bits,
		    .pos = pos & mask,
		};
	}

	template <std::floating_point Float> inline constexpr glm::vec<3, Float> GetCenter() const {
		Float base = Float(1) / Float(1u << level), offset = Float(0.5) * base;
		return {
		    Float(pos.x) * base + offset,
		    Float(pos.y) * base + offset,
		    Float(pos.z) * base + offset,
		};
	}
	template <std::floating_point Float> inline constexpr glm::vec<3, Float> GetLowerBound() const {
		Float base = Float(1) / Float(1u << level);
		return {
		    Float(pos.x) * base,
		    Float(pos.y) * base,
		    Float(pos.z) * base,
		};
	}
	template <std::floating_point Float> inline constexpr glm::vec<3, Float> GetUpperBound() const {
		Float base = Float(1) / Float(1u << level);
		return {
		    Float(pos.x + 1u) * base,
		    Float(pos.y + 1u) * base,
		    Float(pos.z + 1u) * base,
		};
	}
	inline constexpr glm::vec<3, Word> GetLowerBoundAtLevel(Word at_level) const {
		Word bits = at_level - level;
		return pos << bits;
	}
	inline constexpr glm::vec<3, Word> GetUpperBoundAtLevel(Word at_level) const {
		Word bits = at_level - level;
		return (pos + Word(1)) << bits;
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODECOORD_HPP

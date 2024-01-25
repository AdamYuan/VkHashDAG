//
// Created by adamyuan on 1/24/24.
//

#pragma once
#ifndef VKHASHDAG_VBRCOLOR_HPP
#define VKHASHDAG_VBRCOLOR_HPP

#include "Color.hpp"
#include "ColorBlock.hpp"

namespace hashdag {

template <uint32_t MaxWeightBits = 4> class VBRColor {
	static_assert(32 % MaxWeightBits == 0 && MaxWeightBits <= 8);

private:
	uint32_t m_colors;
	uint8_t m_weight;
	bool m_single;

public:
	inline VBRColor() = default;
	inline VBRColor(const glm::vec3 &rgb) : m_colors(RGB8Color{rgb}.GetData()), m_single{true} {}

	inline glm::vec3 Get() const {
		return m_single ? RGB8Color{m_colors}.Get()
		                : glm::mix(R5G6B5Color{m_colors}.Get(), R5G6B5Color{m_colors >> 16u}.Get(),
		                           m_weight / float((1u << MaxWeightBits) - 1));
	}
};

// Variable Bitrate Block Encoding
template <uint32_t MaxWeightBits = 4> class VBRColorBlock {
private:
};

} // namespace hashdag

#endif // VKHASHDAG_VBRCOLOR_HPP

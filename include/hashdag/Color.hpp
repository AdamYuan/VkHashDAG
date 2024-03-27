//
// Created by adamyuan on 1/22/24.
//

#pragma once
#ifndef VKHASHDAG_COLORSVO_COLOR_HPP
#define VKHASHDAG_COLORSVO_COLOR_HPP

#include <cinttypes>
#include <concepts>
#include <glm/glm.hpp>

namespace hashdag {

using RGBColor = glm::vec3;

class RGB8Color {
private:
	glm::u8vec3 m_rgb;

public:
	inline RGB8Color() = default;
	inline RGB8Color(glm::u8vec3 rgb) : m_rgb{rgb} {}
	inline RGB8Color(const RGBColor &rgb) : m_rgb{rgb * 255.0f} {}
	inline RGB8Color(uint32_t data) : m_rgb{data & 0xffu, (data >> 8u) & 0xffu, (data >> 16u) & 0xffu} {}
	inline RGBColor Get() const { return RGBColor(m_rgb) / 255.0f; }
	inline const glm::u8vec3 &GetU8() const { return m_rgb; }

	using DataType = uint32_t;
	inline uint32_t GetData() const { return m_rgb.r | (m_rgb.g << 8u) | (m_rgb.b << 16u); }
};

class R5G6B5Color {
private:
	uint16_t m_data;

public:
	inline R5G6B5Color() = default;
	inline R5G6B5Color(RGBColor rgb) {
		rgb = glm::clamp(rgb, 0.0f, 1.0f) * RGBColor(31, 63, 31);
		m_data = uint16_t(rgb.r) | (uint16_t(rgb.g) << 5u) | (uint16_t(rgb.b) << 11u);
	}
	inline R5G6B5Color(uint16_t data) : m_data{data} {}
	inline RGBColor Get() const {
		return RGBColor((m_data >> 0u) & 0x1fu, (m_data >> 5u) & 0x3fu, (m_data >> 11u) & 0x1fu) / RGBColor(31, 63, 31);
	}

	using DataType = uint16_t;
	inline uint16_t GetData() const { return m_data; }
};

} // namespace hashdag

#endif // VKHASHDAG_COLORSVO_COLOR_HPP

//
// Created by adamyuan on 1/24/24.
//

#pragma once
#ifndef VKHASHDAG_VBRCOLOR_HPP
#define VKHASHDAG_VBRCOLOR_HPP

#include "Color.hpp"
#include "ColorBlock.hpp"

#include <algorithm>
#include <vector>

namespace hashdag {

class VBRColor {
private:
	uint32_t m_colors;
	uint8_t m_weight, m_bits_per_weight;

public:
	inline VBRColor() = default;
	inline VBRColor(const RGBColor &rgb) : m_colors(RGB8Color{rgb}.GetData()), m_bits_per_weight{0} {}
	inline VBRColor(RGB8Color color) : m_colors(color.GetData()), m_bits_per_weight{0} {}
	inline VBRColor(R5G6B5Color left_color, R5G6B5Color right_color, uint8_t weight, uint8_t bits_per_weight)
	    : m_colors(left_color.GetData() | right_color.GetData() << 16u), m_weight{weight},
	      m_bits_per_weight{bits_per_weight} {}

	inline glm::vec3 Get() const {
		return m_bits_per_weight == 0 ? RGB8Color{m_colors}.Get()
		                              : glm::mix(R5G6B5Color(m_colors).Get(), R5G6B5Color(m_colors >> 16u).Get(),
		                                         m_weight / float((1u << m_bits_per_weight) - 1));
	}
};

// Variable Bitrate Block Encoding
struct VBRMacroBlock {
	uint32_t first_block;
	uint32_t weight_start;
};
struct VBRBlockHeader {
	uint32_t colors;
	// uint32_t voxel_index_offset : 14;
	// uint32_t bits_per_weight : 2;
	// uint32_t weight_offset : 16;
	uint32_t packed_14_2_16;
	inline uint32_t GetVoxelIndexOffset() const { return packed_14_2_16 >> 18u; }
	inline uint32_t GetBitsPerWeight() const { return (packed_14_2_16 >> 16u) & 0x3u; }
	inline uint32_t GetWeightOffset() const { return packed_14_2_16 & 0xFFFFu; }

	inline VBRBlockHeader() = default;
	inline VBRBlockHeader(uint32_t colors, uint32_t voxel_index_offset, uint32_t bits_per_weight,
	                      uint32_t weight_offset)
	    : colors{colors}, packed_14_2_16{(voxel_index_offset << 18u) | (bits_per_weight << 16u) | weight_offset} {}
};
class VBRColorBlock {
private:
	static constexpr uint32_t kVoxelBitsPerMacroBlock = 14u;
	static constexpr uint32_t kVoxelsPerMacroBlock = 1u << kVoxelBitsPerMacroBlock;

	std::vector<VBRMacroBlock> m_macro_blocks;
	std::vector<VBRBlockHeader> m_block_headers;
	std::vector<uint32_t> m_weight_bits;

	inline uint32_t get_weight_bits(uint32_t index, uint32_t bits) const {
		const auto get_single = [this](uint32_t index, uint32_t bits) {
			return (m_weight_bits[index >> 5u] >> (index & 31u)) & ((1u << bits) - 1);
		};
		uint32_t offset = index & 31u;
		return offset + bits > 32u
		           ? get_single(index, 32u - offset) | (get_single(index + 1, bits + offset - 32u) << (32u - offset))
		           : get_single(index, bits);
	}

	inline VBRColor get_color(uint32_t voxel_index) const {
		uint32_t macro_block_id = voxel_index >> kVoxelBitsPerMacroBlock,
		         voxel_index_offset = voxel_index & (kVoxelsPerMacroBlock - 1u);

		auto block_begin = m_block_headers.begin() + m_macro_blocks[macro_block_id].first_block,
		     block_end = macro_block_id + 1 == m_macro_blocks.size()
		                     ? m_block_headers.end()
		                     : m_block_headers.begin() + m_macro_blocks[macro_block_id + 1].first_block;

		auto block_it = std::lower_bound(block_begin, block_end, voxel_index_offset,
		                                 [](VBRBlockHeader b, uint32_t i) { return b.GetVoxelIndexOffset() < i; });
		VBRBlockHeader block = *block_it;
		uint32_t weight_bits = block.GetBitsPerWeight();
		if (weight_bits == 0)
			return VBRColor(RGB8Color(block.colors));

		uint32_t weight_index = m_macro_blocks[macro_block_id].weight_start + block.GetWeightOffset();
		return VBRColor{R5G6B5Color(block.colors), R5G6B5Color(block.colors >> 16u),
		                (uint8_t)get_weight_bits(weight_index, weight_bits), (uint8_t)weight_bits};
	}

	friend class VBRColorBlockWriter;

public:
	template <std::unsigned_integral Word> inline VBRColor GetColor(const NodeCoord<Word> &coord) const {
		return get_color(coord.GetMortonIndex());
	}
};

class VBRColorBlockWriter final : public VBRColorBlock {
private:
	VBRColorBlock *m_p_color_block;
	uint32_t m_levels, m_weight_bit_count = 0, m_voxel_count = 0;

	inline void copy(uint32_t voxel_begin, uint32_t voxel_end) {}
	inline void finalize() {
		uint32_t full_voxel_count = 1u << (m_levels * 3);
		if (m_voxel_count >= full_voxel_count)
			return;
		// copy remaining voxels if not full
		copy(m_voxel_count, full_voxel_count);
		m_voxel_count = full_voxel_count;
	}

public:
	inline VBRColorBlockWriter(VBRColorBlock *p_color_block, uint32_t levels)
	    : m_p_color_block{p_color_block}, m_levels{levels} {}
	inline ~VBRColorBlockWriter() { finalize(); }
};

} // namespace hashdag

#endif // VKHASHDAG_VBRCOLOR_HPP

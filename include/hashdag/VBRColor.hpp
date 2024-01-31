//
// Created by adamyuan on 1/24/24.
//

#pragma once
#ifndef VKHASHDAG_VBRCOLOR_HPP
#define VKHASHDAG_VBRCOLOR_HPP

#include "Color.hpp"
#include "ColorBlock.hpp"

#include <algorithm>
#include <libmorton/morton.h>
#include <vector>

namespace hashdag {

inline uint32_t VBRGetMortonIndex(const glm::u32vec3 &pos) {
	return libmorton::morton3D_32_encode(pos.x, pos.y, pos.z);
}

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
		                                         float(m_weight) / float((1u << m_bits_per_weight) - 1));
	}
	inline uint32_t GetColors() const { return m_colors; }
	inline uint8_t GetWeight() const { return m_weight; }
	inline uint8_t GetBitsPerWeight() const { return m_bits_per_weight; }
};

// Variable Bitrate Block Encoding
struct VBRMacroBlock {
	uint32_t first_block;
	uint32_t weight_start;
	auto operator<=>(const VBRMacroBlock &) const = default;
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
	auto operator<=>(const VBRBlockHeader &) const = default;
};

template <std::unsigned_integral Word> class VBRBitset {
private:
	inline static constexpr Word kWordBits = sizeof(Word) << Word(3), kWordMask = kWordBits - Word(1),
	                             kWordMaskBits = std::bit_width(kWordMask);
	std::vector<Word> m_bits;
	std::size_t m_bit_count = 0;

	// unit bits = 1, 2, 3 is guaranteed for VBR

public:
	inline const std::vector<Word> &GetData() const { return m_bits; }
	inline std::size_t Size() const { return m_bit_count; }
	inline void Push(Word word, Word bits, std::size_t count = 1) {
		if (count == 0)
			return;

		Word bit_offset = m_bit_count & kWordMask;
		if (bit_offset == 0)
			m_bits.emplace_back();

		if (count == 1) {
			m_bits.back() |= word << bit_offset;
			if (bit_offset + bits > kWordBits)
				m_bits.push_back(word >> (kWordBits - bit_offset));
			m_bit_count += bits;
			return;
		}

		Word full_word = word | (word << bits);
		for (Word i = 1; (bits << i) < kWordBits; ++i)
			full_word |= full_word << Word(bits << i);

		const auto get_rotr_full_word = [full_word, bits](Word r) -> Word {
			return r ? (Word(full_word >> r) | Word(full_word << (Word(kWordBits / bits) * bits - r))) : full_word;
		};

		std::size_t remain_bits = bits * count, last_bits;
		m_bits.back() |= full_word << bit_offset;
		if (remain_bits + bit_offset <= kWordBits)
			last_bits = remain_bits + bit_offset;
		else {
			remain_bits -= kWordBits - bit_offset;
			last_bits = remain_bits;

			std::size_t word_count = (remain_bits >> kWordMaskBits) + ((remain_bits & kWordMask) ? 1 : 0);
			Word r_rot = (kWordBits - bit_offset) % bits;
			if (bits == 3) {
				Word rotr_full_words[3] = {
				    get_rotr_full_word(0),
				    get_rotr_full_word(1),
				    get_rotr_full_word(2),
				};
				for (uint32_t i = 0; i < word_count; ++i) {
					m_bits.push_back(rotr_full_words[r_rot]);
					static_assert(kWordBits % 3 != 0);
					r_rot = (r_rot + (kWordBits % 3)) % 3;
				}
			} else {
				// bits == 1 or 2, so that kWordBits % bits == 0 and all bits are same across words
				m_bits.resize(m_bits.size() + word_count, get_rotr_full_word(r_rot));
			}
		}
		// Mask out exceeded bits
		if ((last_bits &= kWordMask))
			m_bits.back() &= (Word(1) << last_bits) - Word(1);

		m_bit_count += bits * count;
	}
	inline void Copy(const VBRBitset<Word> &src, std::size_t src_begin, std::size_t src_bits) {
		if (src_bits == 0)
			return;

		Word bit_offset = m_bit_count & kWordMask, src_bit_offset = src_begin & kWordMask;
		if (bit_offset == 0)
			m_bits.emplace_back();

		std::size_t word_count = 0, last_bits; // Additional words

		if (bit_offset + src_bits <= kWordBits)
			last_bits = src_bits + bit_offset;
		else {
			std::size_t remain_bits = src_bits - (kWordBits - bit_offset);
			last_bits = remain_bits;
			word_count = std::size_t(remain_bits >> kWordMaskBits) + std::size_t((remain_bits & kWordMask) ? 1 : 0);
			// printf("SB = %zu, RB = %zu, WC = %zu\n", src_bits, remain_bits, word_count);
		}

		auto src_word_it = src.m_bits.begin() + (src_begin >> kWordMaskBits);
		if (bit_offset == src_bit_offset) {
			// printf("=\n");
			m_bits.back() |= Word(*src_word_it >> bit_offset) << bit_offset;
			m_bits.insert(m_bits.end(), src_word_it + 1, src_word_it + 1 + word_count);
		} else if (src_bit_offset < bit_offset) {
			Word prev_word = *(src_word_it++);
			m_bits.back() |= (prev_word >> src_bit_offset) << bit_offset;
			Word shl = bit_offset - src_bit_offset, prev_shr = kWordBits - shl;
			for (std::size_t i = 0; i < word_count; ++i) {
				Word word = src_word_it == src.m_bits.end() ? Word(0) : *(src_word_it++);
				m_bits.push_back((prev_word >> prev_shr) | (word << shl));
				prev_word = word;
			}
		} else {
			// src_bit_offset > bit_offset
			Word word = *(src_word_it++);
			Word next_word = src_word_it == src.m_bits.end() ? Word(0) : *(src_word_it++);
			Word shr = src_bit_offset - bit_offset, next_shl = kWordBits - shr;
			m_bits.back() |= (word >> src_bit_offset) << bit_offset;
			m_bits.back() |= next_word << next_shl;
			for (std::size_t i = 0; i < word_count; ++i) {
				word = next_word;
				next_word = src_word_it == src.m_bits.end() ? Word(0) : *(src_word_it++);
				m_bits.push_back((word >> shr) | (next_word << next_shl));
			}
		}

		// Mask out exceeded bits
		if ((last_bits &= kWordMask))
			m_bits.back() &= (Word(1) << last_bits) - Word(1);

		m_bit_count += src_bits;
	}
	inline Word Get(std::size_t index, Word bits) const {
		Word offset = index & kWordMask;
		Word w0 = m_bits[index >> kWordMaskBits] >> offset;
		if (offset + bits <= kWordBits) {
			Word bit_mask = (Word(1) << bits) - Word(1);
			return w0 & bit_mask;
		}
		Word w1 = m_bits[(index >> kWordMaskBits) + 1] & ((Word(1) << (bits + offset - kWordBits)) - Word(1));
		return w0 | (w1 << (kWordBits - offset));
	}
	inline void Clear() {
		m_bit_count = 0;
		m_bits.clear();
	}
};

class VBRColorBlock {
#ifdef HASHDAG_TEST
public:
#else
private:
#endif
	static constexpr uint32_t kVoxelBitsPerMacroBlock = 14u;
	static constexpr uint32_t kVoxelsPerMacroBlock = 1u << kVoxelBitsPerMacroBlock;

	std::vector<VBRMacroBlock> m_macro_blocks;
	std::vector<VBRBlockHeader> m_block_headers;
	VBRBitset<uint32_t> m_weight_bits;

	struct VoxelIndexInfo {
		uint32_t macro_id, block_id, block_offset;
	};

	inline VoxelIndexInfo get_voxel_index_info(uint32_t voxel_index) const {
		uint32_t macro_id = voxel_index >> kVoxelBitsPerMacroBlock,
		         macro_offset = voxel_index & (kVoxelsPerMacroBlock - 1u);

		auto block_begin = m_block_headers.begin() + m_macro_blocks[macro_id].first_block,
		     block_end = macro_id + 1 == m_macro_blocks.size()
		                     ? m_block_headers.end()
		                     : m_block_headers.begin() + m_macro_blocks[macro_id + 1].first_block;

		auto block_it = std::upper_bound(block_begin, block_end, macro_offset,
		                                 [](uint32_t i, VBRBlockHeader b) { return i < b.GetVoxelIndexOffset(); }) -
		                1;

		uint32_t block_id = block_it - m_block_headers.begin();
		uint32_t block_offset = macro_offset - block_it->GetVoxelIndexOffset();

		return {macro_id, block_id, block_offset};
	}

	inline VBRColor get_color(uint32_t voxel_index) const {
		auto [macro_id, block_id, block_offset] = get_voxel_index_info(voxel_index);
		VBRBlockHeader block = m_block_headers[block_id];
		uint32_t weight_bits = block.GetBitsPerWeight();
		if (weight_bits == 0)
			return {RGB8Color(block.colors)};

		uint32_t weight_index =
		    m_macro_blocks[macro_id].weight_start + block.GetWeightOffset() + block_offset * weight_bits;
		return VBRColor{R5G6B5Color(block.colors), R5G6B5Color(block.colors >> 16u),
		                (uint8_t)m_weight_bits.Get(weight_index, weight_bits), (uint8_t)weight_bits};
	}

	friend class VBRColorBlockWriter;

public:
	inline virtual ~VBRColorBlock() = default;
	template <std::unsigned_integral Word> inline VBRColor GetColor(const NodeCoord<Word> &coord) const {
		return get_color(VBRGetMortonIndex(coord.pos));
	}
	inline bool Empty() const { return m_macro_blocks.empty(); }
};

class VBRColorBlockWriter final : public VBRColorBlock {
#ifdef HASHDAG_TEST
public:
#else
private:
#endif
	VBRColorBlock *m_p_src;
	uint32_t m_levels, m_voxel_count = 0;

	inline uint32_t get_full_voxel_count() const { return 1u << (m_levels * 3u); }

	inline void append(uint32_t colors, uint32_t bits_per_weight, uint32_t voxel_count, auto &&push_weight_func) {
		for (uint32_t i = 0; i < voxel_count;) {
			uint32_t voxel_index = m_voxel_count + i;

			uint32_t macro_id = voxel_index >> kVoxelBitsPerMacroBlock;
			if (macro_id >= m_macro_blocks.size()) {
				m_macro_blocks.push_back(VBRMacroBlock{
				    .first_block = uint32_t(m_block_headers.size()),
				    .weight_start = uint32_t(m_weight_bits.Size()),
				});
				// assert(voxel_index % kVoxelsPerMacroBlock == 0);
				m_block_headers.emplace_back(colors, 0, bits_per_weight, 0);
			}

			if (colors != m_block_headers.back().colors ||
			    bits_per_weight != m_block_headers.back().GetBitsPerWeight()) {
				m_block_headers.emplace_back(colors, voxel_index & (kVoxelsPerMacroBlock - 1u), bits_per_weight,
				                             uint32_t(m_weight_bits.Size()) - m_macro_blocks.back().weight_start);
			}

			uint32_t append_voxel_count =
			    std::min(voxel_count - i, ((macro_id + 1u) << kVoxelBitsPerMacroBlock) - voxel_index);

			if (bits_per_weight)
				push_weight_func(i, append_voxel_count);

			i += append_voxel_count;
		}

		m_voxel_count += voxel_count;
	}
	inline void copy_voxels(uint32_t src_voxel_begin, uint32_t voxel_count) {
		if (m_p_src->Empty()) {
			append_voxels(VBRColor{}, voxel_count);
			return;
		}

		auto [src_macro_id, src_block_id, src_block_offset] = m_p_src->get_voxel_index_info(src_voxel_begin);

		for (uint32_t i = 0; i < voxel_count;) {
			// Parameters for Copy
			VBRBlockHeader src_block = m_p_src->m_block_headers[src_block_id];
			uint32_t src_weight_base = m_p_src->m_macro_blocks[src_macro_id].weight_start +
			                           src_block.GetWeightOffset() + src_block_offset * src_block.GetBitsPerWeight();

			// Get Copy count, update Source info
			uint32_t copy_voxel_count = voxel_count - i;
			if (src_block_id + 1 < m_p_src->m_block_headers.size()) {
				uint32_t next_block_offset = m_p_src->m_block_headers[src_block_id + 1].GetVoxelIndexOffset();
				uint32_t src_macro_offset =
				    m_p_src->m_block_headers[src_block_id].GetVoxelIndexOffset() + src_block_offset;

				if (next_block_offset == 0) {
					++src_macro_id;
					copy_voxel_count = std::min(copy_voxel_count, kVoxelsPerMacroBlock - src_macro_offset);
				} else
					copy_voxel_count = std::min(copy_voxel_count, next_block_offset - src_macro_offset);

				++src_block_id;
				src_block_offset = 0u;
			}
			i += copy_voxel_count;

			// Append colors in src_block
			append(
			    src_block.colors, src_block.GetBitsPerWeight(), copy_voxel_count, [&](uint32_t offset, uint32_t count) {
				    m_weight_bits.Copy(m_p_src->m_weight_bits, src_weight_base + offset * src_block.GetBitsPerWeight(),
				                       count * src_block.GetBitsPerWeight());
			    });
		}
	}

	inline void append_voxels(VBRColor color, uint32_t voxel_count) {
		append(color.GetColors(), color.GetBitsPerWeight(), voxel_count,
		       [this, color](uint32_t offset, uint32_t count) {
			       m_weight_bits.Push(color.GetWeight(), color.GetBitsPerWeight(), count);
		       });
	}
	inline void finalize() {
		uint32_t full_voxel_count = get_full_voxel_count();
		if (m_voxel_count < full_voxel_count)
			copy_voxels(m_voxel_count, full_voxel_count - m_voxel_count);
	}

public:
	inline VBRColorBlockWriter(VBRColorBlock *p_src, uint32_t levels) : m_p_src{p_src}, m_levels{levels} {}
	inline ~VBRColorBlockWriter() override { Flush(); }
	inline void Flush() {
		if (Empty())
			return;

		finalize();
		m_p_src->m_macro_blocks = std::move(m_macro_blocks);
		m_p_src->m_block_headers = std::move(m_block_headers);
		m_p_src->m_weight_bits = std::move(m_weight_bits);

		m_macro_blocks.clear();
		m_block_headers.clear();
		m_weight_bits.Clear();
	}
	template <std::unsigned_integral Word> inline void SetColor(const NodeCoord<Word> &coord, VBRColor color) {
		uint32_t begin = VBRGetMortonIndex(coord.GetLowerBoundAtLevel(m_levels - 1)),
		         end = VBRGetMortonIndex(coord.GetUpperBoundAtLevel(m_levels - 1) - glm::vec<3, Word>(1)) + 1u;
		if (m_voxel_count < begin)
			copy_voxels(m_voxel_count, begin - m_voxel_count);
		append_voxels(color, end - begin);
	}
};

static_assert(ColorBlock<VBRColorBlock, uint32_t, VBRColor>);
static_assert(ColorBlockWriter<VBRColorBlockWriter, uint32_t, VBRColor>);

} // namespace hashdag

#endif // VKHASHDAG_VBRCOLOR_HPP

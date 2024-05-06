//
// Created by adamyuan on 1/24/24.
//

#pragma once
#ifndef VKHASHDAG_VBRCOLOR_HPP
#define VKHASHDAG_VBRCOLOR_HPP

#include "Color.hpp"

#include <algorithm>
#include <concepts>
#include <iostream>
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
		                                         float(m_weight) / float((1u << m_bits_per_weight) - 1));
	}
	inline uint32_t GetColors() const { return m_colors; }
	inline uint8_t GetWeight() const { return m_weight; }
	inline uint8_t GetBitsPerWeight() const { return m_bits_per_weight; }
};

struct VBRInfo {
	static constexpr uint32_t kVoxelBitsPerMacroBlock = 14u;
	static constexpr uint32_t kVoxelsPerMacroBlock = 1u << kVoxelBitsPerMacroBlock;
};

template <std::unsigned_integral Word> struct VBRWordInfo {
	inline static constexpr Word kBits = sizeof(Word) << Word(3), kMask = kBits - Word(1),
	                             kMaskBits = std::bit_width(kMask);
};

template <std::unsigned_integral Word, template <typename> typename Container> class VBRBitset {
private:
	inline static constexpr Word kWordBits = VBRWordInfo<Word>::kBits, kWordMask = VBRWordInfo<Word>::kMask,
	                             kWordMaskBits = VBRWordInfo<Word>::kMaskBits;
	Container<Word> m_bits;
	static_assert(std::ranges::random_access_range<Container<Word>>);

	template <std::unsigned_integral, template <typename> typename> friend class VBRBitset;
	template <std::unsigned_integral> friend class VBRBitsetWriter;

public:
	inline VBRBitset() = default;
	inline explicit VBRBitset(Container<Word> bits) : m_bits{std::move(bits)} {}
	template <template <typename> typename SrcContainer>
	inline explicit VBRBitset(const VBRBitset<Word, SrcContainer> &src) : m_bits{src.m_bits} {}
	template <template <typename> typename SrcContainer>
	inline explicit VBRBitset(VBRBitset<Word, SrcContainer> &&src) : m_bits{std::move(src.m_bits)} {}

	inline const Container<Word> &GetBits() const { return m_bits; }
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
};

template <std::unsigned_integral Word> class VBRBitsetWriter {
private:
	inline static constexpr Word kWordBits = VBRWordInfo<Word>::kBits, kWordMask = VBRWordInfo<Word>::kMask,
	                             kWordMaskBits = VBRWordInfo<Word>::kMaskBits;

	std::vector<Word> m_bits;
	std::size_t m_bit_count = 0;

public:
	inline std::size_t GetBitCount() const { return m_bit_count; }
	inline VBRBitset<Word, std::vector> Flush() { return VBRBitset<Word, std::vector>{std::move(m_bits)}; }

	// unit bits = 0, 1, 2, 3 is guaranteed for VBR
	inline void Push(Word word, Word bits) {
		if (bits == 0)
			return;
		Word bit_offset = m_bit_count & kWordMask;
		if (bit_offset == 0)
			m_bits.emplace_back();
		m_bits.back() |= word << bit_offset;
		if (bit_offset + bits > kWordBits)
			m_bits.push_back(word >> (kWordBits - bit_offset));
		m_bit_count += bits;
	}
	inline void Push(Word word, Word bits, std::size_t count) {
		if (count == 0 || bits == 0)
			return;

		Word bit_offset = m_bit_count & kWordMask;
		if (bit_offset == 0)
			m_bits.emplace_back();

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

	template <template <typename> typename SrcContainer>
	inline void Copy(const VBRBitset<Word, SrcContainer> &src, std::size_t src_begin, std::size_t src_bits) {
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

template <std::unsigned_integral Word, template <typename> typename Container> class VBRChunkIterator;

template <std::unsigned_integral Word, template <typename> typename Container> class VBRChunk {
#ifdef HASHDAG_TEST
public:
#else
private:
#endif
	Container<VBRMacroBlock> m_macro_blocks;
	Container<VBRBlockHeader> m_block_headers;
	VBRBitset<Word, Container> m_weight_bits;

	static_assert(std::ranges::random_access_range<Container<VBRMacroBlock>>);
	static_assert(std::ranges::random_access_range<Container<VBRMacroBlock>>);

	template <std::unsigned_integral, template <typename> typename> friend class VBRChunk;
	template <std::unsigned_integral, template <typename> typename> friend class VBRChunkWriter;
	template <std::unsigned_integral, template <typename> typename> friend class VBRChunkIterator;

public:
	inline VBRChunk() = default;
	template <template <typename> typename SrcContainer>
	inline explicit VBRChunk(const VBRChunk<Word, SrcContainer> &src)
	    : m_macro_blocks{src.m_macro_blocks}, m_block_headers{src.m_block_headers}, m_weight_bits{src.m_weight_bits} {}
	template <template <typename> typename SrcContainer>
	inline explicit VBRChunk(VBRChunk<Word, SrcContainer> &&src)
	    : m_macro_blocks{std::move(src.m_macro_blocks)}, m_block_headers{std::move(src.m_block_headers)},
	      m_weight_bits{std::move(src.m_weight_bits)} {}

	inline VBRChunk(Container<VBRMacroBlock> macro_blocks, Container<VBRBlockHeader> block_headers,
	                VBRBitset<Word, Container> weight_bits)
	    : m_macro_blocks{std::move(macro_blocks)}, m_block_headers{std::move(block_headers)},
	      m_weight_bits{std::move(weight_bits)} {}

	inline bool Empty() const { return m_macro_blocks.empty(); }
};

template <std::unsigned_integral Word, template <typename> typename Container> class VBRChunkIterator {
private:
	static constexpr uint32_t kVoxelBitsPerMacroBlock = VBRInfo::kVoxelBitsPerMacroBlock;
	static constexpr uint32_t kVoxelsPerMacroBlock = VBRInfo::kVoxelsPerMacroBlock;

	VBRChunk<Word, Container> m_chunk;
	uint32_t m_macro_id{}, m_block_id{}, m_block_offset{};

	inline bool is_last_block() const { return m_block_id == m_chunk.m_block_headers.size() - 1; }
	inline uint32_t get_block_size() const {
		assert(!is_last_block());
		uint32_t next_voxel_offset = m_chunk.m_block_headers[m_block_id + 1].GetVoxelIndexOffset();
		next_voxel_offset = next_voxel_offset == 0u ? kVoxelsPerMacroBlock : next_voxel_offset;
		return next_voxel_offset - m_chunk.m_block_headers[m_block_id].GetVoxelIndexOffset();
	}
	inline void next_block() {
		assert(!is_last_block());
		m_block_offset = 0;
		++m_block_id;
		m_macro_id += GetBlockHeader().GetVoxelIndexOffset() == 0;
	}

public:
	inline VBRChunkIterator() = default;
	template <template <typename> typename SrcContainer>
	inline explicit VBRChunkIterator(const VBRChunk<Word, SrcContainer> &src) : m_chunk{src} {}
	template <template <typename> typename SrcContainer>
	inline explicit VBRChunkIterator(VBRChunk<Word, SrcContainer> &&src) : m_chunk{std::move(src)} {}

	inline bool Empty() const { return m_chunk.Empty(); }
	inline const VBRChunk<Word, Container> &GetChunk() const { return m_chunk; }

	inline const VBRMacroBlock &GetMacroBlock() const { return m_chunk.m_macro_blocks[m_macro_id]; }
	inline const VBRBlockHeader &GetBlockHeader() const { return m_chunk.m_block_headers[m_block_id]; }
	inline uint32_t GetVoxelIndex() const {
		return (m_macro_id << kVoxelBitsPerMacroBlock) | (GetBlockHeader().GetVoxelIndexOffset() + m_block_offset);
	}
	inline uint32_t GetWeightIndex() const {
		const VBRBlockHeader &block = GetBlockHeader();
		return GetMacroBlock().weight_start + block.GetWeightOffset() + m_block_offset * block.GetBitsPerWeight();
	}
	inline VBRColor GetColor() const {
		const VBRBlockHeader &block = GetBlockHeader();
		uint32_t weight_bits = block.GetBitsPerWeight();
		if (weight_bits == 0)
			return {RGB8Color(block.colors)};
		return VBRColor{R5G6B5Color(block.colors), R5G6B5Color(block.colors >> 16u),
		                (uint8_t)m_chunk.m_weight_bits.Get(GetWeightIndex(), weight_bits), (uint8_t)weight_bits};
	}
	inline void Next() {
		Next([](auto &&) {});
	}
	inline void Next(std::invocable<const VBRChunkIterator &> auto &&on_move_one) {
		on_move_one(*this);
		++m_block_offset;
		if (!is_last_block() && m_block_offset == get_block_size())
			next_block();
	}
	inline void Jump(uint32_t count) {
		Jump(count, [](auto &&, auto &&) {});
	}
	inline void Jump(uint32_t count, std::invocable<const VBRChunkIterator &, uint32_t> auto &&on_move) {
		if (count == 0)
			return;
		uint32_t step_count;
		while (!is_last_block() && count >= (step_count = get_block_size() - m_block_offset)) {
			on_move(*this, step_count);
			count -= step_count;
			next_block();
		}
		if (count) {
			on_move(*this, count);
			m_block_offset += count;
		}
	}
};

template <std::unsigned_integral Word, template <typename> typename SrcContainer> class VBRChunkWriter {
#ifdef HASHDAG_TEST
public:
#else
private:
#endif
	static constexpr uint32_t kVoxelBitsPerMacroBlock = VBRInfo::kVoxelBitsPerMacroBlock;
	static constexpr uint32_t kVoxelsPerMacroBlock = VBRInfo::kVoxelsPerMacroBlock;

	std::vector<VBRMacroBlock> m_macro_blocks;
	std::vector<VBRBlockHeader> m_block_headers;
	VBRBitsetWriter<Word> m_weight_bits;

	VBRChunkIterator<Word, SrcContainer> m_src_iterator;
	uint32_t m_voxel_count = 0;

	inline void append(uint32_t colors, uint32_t bits_per_weight, uint32_t voxel_count, uint32_t &weight_start) {
		for (uint32_t i = 0; i < voxel_count;) {
			uint32_t voxel_index = m_voxel_count + i;

			uint32_t macro_id = voxel_index >> kVoxelBitsPerMacroBlock;
			if (macro_id >= m_macro_blocks.size()) {
				m_macro_blocks.push_back(VBRMacroBlock{
				    .first_block = uint32_t(m_block_headers.size()),
				    .weight_start = weight_start,
				});
				// assert(voxel_index % kVoxelsPerMacroBlock == 0);
				m_block_headers.emplace_back(colors, 0, bits_per_weight, 0);
			}

			if (colors != m_block_headers.back().colors ||
			    bits_per_weight != m_block_headers.back().GetBitsPerWeight()) {
				m_block_headers.emplace_back(colors, voxel_index & (kVoxelsPerMacroBlock - 1u), bits_per_weight,
				                             weight_start - m_macro_blocks.back().weight_start);
			}

			uint32_t append_voxel_count =
			    std::min(voxel_count - i, ((macro_id + 1u) << kVoxelBitsPerMacroBlock) - voxel_index);
			weight_start += append_voxel_count * bits_per_weight;
			i += append_voxel_count;
		}

		m_voxel_count += voxel_count;
	}

	inline void append_one(uint32_t colors, uint32_t bits_per_weight, uint32_t &weight_start) {
		uint32_t voxel_index = m_voxel_count;

		uint32_t macro_id = voxel_index >> kVoxelBitsPerMacroBlock;
		if (macro_id >= m_macro_blocks.size()) {
			m_macro_blocks.push_back(VBRMacroBlock{
			    .first_block = uint32_t(m_block_headers.size()),
			    .weight_start = weight_start,
			});
			// assert(voxel_index % kVoxelsPerMacroBlock == 0);
			m_block_headers.emplace_back(colors, 0, bits_per_weight, 0);
		}

		if (colors != m_block_headers.back().colors || bits_per_weight != m_block_headers.back().GetBitsPerWeight()) {
			m_block_headers.emplace_back(colors, voxel_index & (kVoxelsPerMacroBlock - 1u), bits_per_weight,
			                             weight_start - m_macro_blocks.back().weight_start);
		}
		weight_start += bits_per_weight;

		++m_voxel_count;
	}

	inline void copy(uint32_t count) {
		uint32_t weight_start = m_weight_bits.GetBitCount(), prev_weight_start = weight_start,
		         src_weight_start = m_src_iterator.GetWeightIndex();
		m_src_iterator.Jump(count, [&](const VBRChunkIterator<Word, SrcContainer> &iterator, uint32_t count) {
			VBRBlockHeader block = iterator.GetBlockHeader();
			append(block.colors, block.GetBitsPerWeight(), count, weight_start);
		});
		m_weight_bits.Copy(m_src_iterator.GetChunk().m_weight_bits, src_weight_start, weight_start - prev_weight_start);
	}
	inline void push(VBRColor color, uint32_t count) {
		uint32_t weight_start = m_weight_bits.GetBitCount();
		append(color.GetColors(), color.GetBitsPerWeight(), count, weight_start);
		m_weight_bits.Push(color.GetWeight(), color.GetBitsPerWeight(), count);
	}
	inline void push_one(VBRColor color) {
		uint32_t weight_start = m_weight_bits.GetBitCount();
		append_one(color.GetColors(), color.GetBitsPerWeight(), weight_start);
		m_weight_bits.Push(color.GetWeight(), color.GetBitsPerWeight());
	}

public:
	inline VBRChunkWriter() = default;
	template <template <typename> typename Container>
	inline explicit VBRChunkWriter(const VBRChunk<Word, Container> &chunk) : m_src_iterator(chunk) {}
	inline explicit VBRChunkWriter(const VBRChunkIterator<Word, SrcContainer> &src_iterator)
	    : m_src_iterator(src_iterator) {}
	inline uint32_t GetVoxelCount() const { return m_voxel_count; }
	inline VBRChunk<Word, std::vector> Flush() {
		return VBRChunk<Word, std::vector>{std::move(m_macro_blocks), std::move(m_block_headers),
		                                   m_weight_bits.Flush()};
	}
	inline void Copy(uint32_t voxel_count) {
		if (m_src_iterator.Empty()) {
			push(VBRColor{}, voxel_count);
			return;
		}
		copy(voxel_count);
	}
	inline void Push(VBRColor color, uint32_t voxel_count) {
		push(color, voxel_count);
		if (!m_src_iterator.Empty())
			m_src_iterator.Jump(voxel_count);
	}
	inline void Edit(std::invocable<VBRColor &> auto &&editor) {
		VBRColor color = {};
		if (!m_src_iterator.Empty())
			m_src_iterator.Next(
			    [&](const VBRChunkIterator<Word, SrcContainer> &iterator) { color = iterator.GetColor(); });
		editor(color);
		push_one(color);
	}
};

} // namespace hashdag

#endif // VKHASHDAG_VBRCOLOR_HPP

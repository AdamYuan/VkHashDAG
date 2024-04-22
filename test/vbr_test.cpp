#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <hashdag/VBRColor.hpp>
#include <span>

template <typename T> void vector_cmp(const std::vector<T> &l, const std::vector<T> &r) {
	CHECK_EQ(l.size(), r.size());
	if (l.size() == r.size())
		for (std::size_t i = 0; i < l.size(); ++i)
			CHECK_EQ(l[i], r[i]);
}

template <typename Word> void test_push_get() {
	for (Word bits = 1; bits < 4; ++bits) {
		for (Word word = 0; word < ((Word)1 << bits); ++word) {
			Word bits2 = bits == 3 ? 1 : bits + 1, word2 = word & ((Word(1) << bits2) - Word(1));

			hashdag::VBRBitsetWriter<Word> bitset_w;
			bitset_w.Push(word, bits, 100);
			bitset_w.Push(word2, bits2, 100000);
			auto bitset = bitset_w.Flush();

			hashdag::VBRBitsetWriter<Word> bitset2_w;
			for (std::size_t i = 0; i < 100; ++i)
				bitset2_w.Push(word, bits);
			for (std::size_t i = 0; i < 100000; ++i)
				bitset2_w.Push(word2, bits2);
			auto bitset2 = bitset2_w.Flush();

			vector_cmp(bitset.GetBits(), bitset2.GetBits());

			for (std::size_t i = 0; i < 100; ++i)
				CHECK_EQ(bitset.Get(i * bits, bits), word);
			for (std::size_t i = 0; i < 100000; ++i)
				CHECK_EQ(bitset.Get(100 * bits + i * bits2, bits2), word2);

			for (std::size_t i = 0; i < 100; ++i)
				CHECK_EQ(bitset2.Get(i * bits, bits), word);
			for (std::size_t i = 0; i < 100000; ++i)
				CHECK_EQ(bitset2.Get(100 * bits + i * bits2, bits2), word2);

			// Aligned Copy-Paste
			{
				hashdag::VBRBitsetWriter<Word> bitset3_w;
				bitset3_w.Push(word, bits, 47);
				bitset3_w.Copy(bitset, 47 * bits, (100 - 47) * bits + 100 * bits2);
				bitset3_w.Copy(bitset2, 100 * bits + 100 * bits2, (100000 - 100) * bits2);

				auto bitset3 = bitset3_w.Flush();

				vector_cmp(bitset.GetBits(), bitset3.GetBits());
				for (std::size_t i = 0; i < 100; ++i)
					CHECK_EQ(bitset3.Get(i * bits, bits), word);
				for (std::size_t i = 0; i < 100000; ++i)
					CHECK_EQ(bitset3.Get(100 * bits + i * bits2, bits2), word2);
			}

			// Non-Aligned Copy-Paste
			{
				hashdag::VBRBitsetWriter<Word> bitset4_w;
				bitset4_w.Push(word, bits, 3);
				bitset4_w.Copy(bitset, 53 * bits, 47 * bits + 100000 * bits2);

				hashdag::VBRBitsetWriter<Word> bitset5_w;
				bitset5_w.Push(word, bits, 46);
				bitset5_w.Copy(bitset, 96 * bits, 3 * bits);
				bitset5_w.Copy(bitset, 99 * bits, bits + 100000 * bits2);

				hashdag::VBRBitsetWriter<Word> bitset6_w;
				bitset6_w.Push(word, bits, 50);
				bitset6_w.Push(word2, bits2, 100000);

				auto bitset4 = bitset4_w.Flush();
				auto bitset5 = bitset5_w.Flush();
				auto bitset6 = bitset6_w.Flush();

				vector_cmp(bitset4.GetBits(), bitset5.GetBits());
				vector_cmp(bitset4.GetBits(), bitset6.GetBits());

				for (std::size_t i = 0; i < 50; ++i)
					CHECK_EQ(bitset4.Get(i * bits, bits), word);
				for (std::size_t i = 0; i < 100000; ++i)
					CHECK_EQ(bitset4.Get(50 * bits + i * bits2, bits2), word2);
			}
		}
	}
}

TEST_CASE("Test VBRBitset") {
	test_push_get<uint8_t>();
	test_push_get<uint16_t>();
	test_push_get<uint32_t>();
	test_push_get<uint64_t>();
}
template <typename T> using const_span = std::span<const T>;
TEST_CASE("Test VBRChunk") {
	constexpr uint32_t kR2 = 10007, kR3 = 21753;
	hashdag::VBRChunk<uint32_t, std::vector> blk;
	hashdag::R5G6B5Color r565{hashdag::RGBColor(1, 0, 0)}, g565{hashdag::RGBColor(0, 1, 0)},
	    b565{hashdag::RGBColor(0, 0, 1)};

	// Basic Appends
	{
		hashdag::VBRChunkWriter<uint32_t, const_span> writer{};
		writer.Push(hashdag::VBRColor{r565, g565, 0b111, 3}, kR2);
		writer.Push(hashdag::VBRColor{r565, g565, 0b000, 3}, kR2);
		writer.Push(hashdag::VBRColor{r565, g565, 0b10, 2}, kR2);
		writer.Push(hashdag::VBRColor{r565, g565, 0b1, 1}, kR2);
		writer.Push(hashdag::VBRColor{hashdag::RGB8Color{0x00FFFFFFu}}, kR2);
		writer.Copy(kR2); // Empty Copy
		CHECK_EQ(writer.m_weight_bits.GetBitCount(), (3 + 3 + 2 + 1) * kR2);
		blk = writer.Flush();
	}

	for (uint32_t i = 0; i < kR2; ++i) {
		CHECK_EQ(blk.Find(i).GetColor().GetBitsPerWeight(), 3);
		CHECK_EQ(blk.Find(i).GetColor().Get(), glm::vec3(0, 1, 0));
		CHECK_EQ(blk.Find(i + kR2).GetColor().GetBitsPerWeight(), 3);
		CHECK_EQ(blk.Find(i + kR2).GetColor().Get(), glm::vec3(1, 0, 0));
		CHECK_EQ(blk.Find(i + kR2 * 2).GetColor().GetBitsPerWeight(), 2);
		CHECK_EQ(blk.Find(i + kR2 * 3).GetColor().GetBitsPerWeight(), 1);
		CHECK_EQ(blk.Find(i + kR2 * 4).GetColor().GetBitsPerWeight(), 0);
		CHECK_EQ(blk.Find(i + kR2 * 4).GetColor().Get(), glm::vec3(1, 1, 1));
	}

	// Check Copy Equality
	{
		hashdag::VBRChunkWriter<uint32_t, const_span> writer{blk};
		writer.Copy(6 * kR2);
		auto blk2 = writer.Flush();
		vector_cmp(blk2.m_block_headers, blk.m_block_headers);
		vector_cmp(blk2.m_macro_blocks, blk.m_macro_blocks);
		vector_cmp(blk2.m_weight_bits.GetBits(), blk.m_weight_bits.GetBits());
	}

	// Check Complex Copy
	{
		hashdag::VBRChunkWriter<uint32_t, const_span> writer{blk};
		writer.Copy(kR2);
		writer.Push(hashdag::VBRColor{r565, g565, 0b000, 3}, kR2);
		writer.Copy(4 * kR2);
		auto blk3 = writer.Flush();
		vector_cmp(blk3.m_block_headers, blk.m_block_headers);
		vector_cmp(blk3.m_macro_blocks, blk.m_macro_blocks);
		vector_cmp(blk3.m_weight_bits.GetBits(), blk.m_weight_bits.GetBits());
	}
}

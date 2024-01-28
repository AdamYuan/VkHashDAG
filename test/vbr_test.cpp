#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <hashdag/VBRColor.hpp>

template <typename Word> void test_push_get() {
	for (Word bits = 1; bits <= 4; ++bits) {
		for (Word word = 0; word < ((Word)1 << bits); ++word) {
			Word bits2 = bits == 4 ? 1 : bits + 1, word2 = word & ((Word(1) << bits2) - Word(1));

			hashdag::VBRBitset<Word> bitset;
			bitset.Push(word, bits, 100);
			bitset.Push(word2, bits2, 100000);

			hashdag::VBRBitset<Word> bitset2;
			for (std::size_t i = 0; i < 100; ++i)
				bitset2.Push(word, bits);
			for (std::size_t i = 0; i < 100000; ++i)
				bitset2.Push(word2, bits2);

			CHECK_EQ(bitset.GetData().size(), bitset2.GetData().size());
			for (std::size_t i = 0; i < bitset.GetData().size(); ++i)
				CHECK_EQ(bitset.GetData()[i], bitset2.GetData()[i]);

			for (std::size_t i = 0; i < 100; ++i)
				CHECK_EQ(bitset.Get(i * bits, bits), word);
			for (std::size_t i = 0; i < 100000; ++i)
				CHECK_EQ(bitset.Get(100 * bits + i * bits2, bits2), word2);

			for (std::size_t i = 0; i < 100; ++i)
				CHECK_EQ(bitset2.Get(i * bits, bits), word);
			for (std::size_t i = 0; i < 100000; ++i)
				CHECK_EQ(bitset2.Get(100 * bits + i * bits2, bits2), word2);

			hashdag::VBRBitset<Word> bitset3;
			bitset3.Push(word, bits, 99);
			bitset3.Paste(bitset, 99 * bits, bits + 100000 * bits2);

			CHECK_EQ(bitset.GetData().size(), bitset3.GetData().size());
			for (std::size_t i = 0; i < bitset.GetData().size(); ++i)
				CHECK_EQ(bitset.GetData()[i], bitset3.GetData()[i]);
			for (std::size_t i = 0; i < 100; ++i)
				CHECK_EQ(bitset3.Get(i * bits, bits), word);
			for (std::size_t i = 0; i < 100000; ++i)
				CHECK_EQ(bitset3.Get(100 * bits + i * bits2, bits2), word2);
		}
	}
}

TEST_CASE("Test VBRBitset::Push() & VBRBitset::Get()") {
	test_push_get<uint8_t>();
	test_push_get<uint16_t>();
	test_push_get<uint32_t>();
	test_push_get<uint64_t>();
}
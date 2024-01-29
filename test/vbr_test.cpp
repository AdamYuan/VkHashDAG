#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <hashdag/VBRColor.hpp>

template <typename T> void vector_cmp(const std::vector<T> &l, const std::vector<T> &r) {
	CHECK_EQ(l.size(), r.size());
	if (l.size() == r.size())
		for (std::size_t i = 0; i < l.size(); ++i)
			CHECK_EQ(l[i], r[i]);
}

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

			vector_cmp(bitset.GetData(), bitset2.GetData());

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
				hashdag::VBRBitset<Word> bitset3;
				bitset3.Push(word, bits, 47);
				bitset3.Copy(bitset, 47 * bits, (100 - 47) * bits + 100 * bits2);
				bitset3.Copy(bitset2, 100 * bits + 100 * bits2, (100000 - 100) * bits2);

				vector_cmp(bitset.GetData(), bitset3.GetData());
				for (std::size_t i = 0; i < 100; ++i)
					CHECK_EQ(bitset3.Get(i * bits, bits), word);
				for (std::size_t i = 0; i < 100000; ++i)
					CHECK_EQ(bitset3.Get(100 * bits + i * bits2, bits2), word2);
			}

			// Non-Aligned Copy-Paste
			{
				hashdag::VBRBitset<Word> bitset4;
				bitset4.Push(word, bits, 3);
				bitset4.Copy(bitset, 53 * bits, 47 * bits + 100000 * bits2);

				hashdag::VBRBitset<Word> bitset5;
				bitset5.Push(word, bits, 17);
				bitset5.Copy(bitset, 67 * bits, 33 * bits + 100000 * bits2);

				vector_cmp(bitset4.GetData(), bitset5.GetData());
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
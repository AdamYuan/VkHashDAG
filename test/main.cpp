#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <hashdag/NodePool.hpp>

struct ZeroHasher {
	inline uint32_t operator()(std::span<const uint32_t> word_span) const { return 0; }
};

struct MurmurHasher {
	inline uint32_t operator()(std::span<const uint32_t> word_span) const {
		uint32_t h = 0; // seed
		for (uint32_t k : word_span) {
			k *= 0xcc9e2d51;
			k = (k << 15) | (k >> 17);
			k *= 0x1b873593;
			h ^= k;
			h = (h << 13) | (h >> 19);
			h = h * 5 + 0xe6546b64;
		}
		h ^= uint32_t(word_span.size());
		h ^= h >> 16;
		h *= 0x85ebca6b;
		h ^= h >> 13;
		h *= 0xc2b2ae35;
		h ^= h >> 16;
		return h;
	}
	inline uint32_t operator()(std::span<const uint32_t, 2> word_span) const {
		uint64_t h = word_span[0] | ((uint64_t)word_span[1] << 32ULL);
		h ^= h >> 33;
		h *= 0xff51afd7ed558ccd;
		h ^= h >> 33;
		h *= 0xc4ceb9fe1a85ec53;
		h ^= h >> 33;
		return h;
	}
};

struct ZeroNodePool final : public hashdag::NodePoolBase<ZeroNodePool, uint32_t, ZeroHasher> {
	std::vector<uint32_t> memory;
	inline explicit ZeroNodePool(uint32_t level_count)
	    : hashdag::NodePoolBase<ZeroNodePool, uint32_t, ZeroHasher>(
	          hashdag::NodeConfig<uint32_t>::MakeDefault(level_count)) {
		memory.resize(GetNodeConfig().GetTotalBuckets() * GetNodeConfig().GetWordsPerBucket());
	}

	inline const uint32_t *ReadPage(uint32_t page_id) const {
		return memory.data() + (page_id << GetNodeConfig().word_bits_per_page);
	}
	inline void ZeroPage(uint32_t page_id, uint32_t page_offset, uint32_t zero_words) {}
	inline void WritePage(uint32_t page_id, uint32_t page_offset, std::span<const uint32_t> word_span) {
		std::copy(word_span.begin(), word_span.end(),
		          memory.data() + ((page_id << GetNodeConfig().word_bits_per_page) | page_offset));
	}
};
struct MurmurNodePool final : public hashdag::NodePoolBase<MurmurNodePool, uint32_t, MurmurHasher> {
	std::vector<uint32_t> memory;
	inline explicit MurmurNodePool(uint32_t level_count)
	    : hashdag::NodePoolBase<MurmurNodePool, uint32_t, MurmurHasher>(
	          hashdag::NodeConfig<uint32_t>::MakeDefault(level_count)) {
		memory.resize(GetNodeConfig().GetTotalWords());
	}

	inline const uint32_t *ReadPage(uint32_t page_id) const {
		return memory.data() + (page_id << GetNodeConfig().word_bits_per_page);
	}
	inline void ZeroPage(uint32_t page_id, uint32_t page_offset, uint32_t zero_words) {}
	inline void WritePage(uint32_t page_id, uint32_t page_offset, std::span<const uint32_t> word_span) {
		std::copy(word_span.begin(), word_span.end(),
		          memory.data() + ((page_id << GetNodeConfig().word_bits_per_page) | page_offset));
	}
};

TEST_SUITE("NodePool") {
	TEST_CASE("Test upsert()") {
		MurmurNodePool pool(4);
		std::vector<uint32_t> node0 = {0b11u, 0x23, 0x45};
		auto opt_idx = pool.UpsertNode(0, node0);
		CHECK(opt_idx);

		auto opt_idx2 = pool.UpsertNode(0, node0);
		CHECK(opt_idx2);
		CHECK_EQ(*opt_idx, *opt_idx2);
		CHECK_EQ(pool.m_bucket_word_counts[*opt_idx / pool.GetNodeConfig().GetWordsPerBucket()], 3);

		std::vector<uint32_t> node1 = {0b110u, 0x23, 0x44};
		auto opt_idx3 = pool.UpsertNode(0, node1);
		CHECK(opt_idx3);
		CHECK_NE(*opt_idx, *opt_idx3);

		auto opt_idx4 = pool.UpsertNode(1, node1);
		CHECK(opt_idx4);
		CHECK_NE(*opt_idx4, *opt_idx3);

		std::vector<uint32_t> node2 = {0b11111111u, 0x23, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x01};
		auto opt_idx5 = pool.UpsertNode(2, node2);
		CHECK(opt_idx5);
		CHECK_EQ(pool.m_bucket_word_counts[*opt_idx5 / pool.GetNodeConfig().GetWordsPerBucket()], 9);

		std::array<uint32_t, 2> leaf0 = {0x23, 0x55};
		auto opt_idx6 = pool.UpsertLeaf(3, leaf0);
		CHECK(opt_idx6);
		CHECK_EQ(pool.m_bucket_word_counts[*opt_idx6 / pool.GetNodeConfig().GetWordsPerBucket()], 2);
	}
	TEST_CASE("Test upsert() bucket full") {
		ZeroNodePool pool(4);
		uint32_t cnt = 0;
		while (true) {
			std::vector<uint32_t> node = {0b11u, cnt, 0x45};
			auto opt_idx = pool.UpsertNode(0, node);
			if (!opt_idx)
				break;
			CHECK(((*opt_idx % pool.GetNodeConfig().GetWordsPerPage()) % 3 == 0));
			++cnt;
		}
		CHECK_EQ(cnt, (pool.GetNodeConfig().GetWordsPerPage() / 3) * pool.GetNodeConfig().GetPagesPerBucket());
	}
}

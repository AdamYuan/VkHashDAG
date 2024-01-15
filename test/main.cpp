#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <hashdag/NodePool.hpp>
#include <unordered_map>

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

struct AABBEditor {
	uint32_t level;
	hashdag::Vec3<uint32_t> aabb_min, aabb_max;
	inline bool IsAffected(const hashdag::NodeCoord<uint32_t> &coord) const {
		auto lb = coord.GetLowerBoundAtLevel(level), ub = coord.GetUpperBoundAtLevel(level);
		/* printf("(%d %d %d), (%d, %d, %d) -> %d\n", lb.x, lb.y, lb.z, ub.x, ub.y, ub.z,
		       !ub.Any(std::less_equal<uint32_t>{}, aabb_min) && !lb.Any(std::greater_equal<uint32_t>{}, aabb_max)); */
		return !ub.Any(std::less_equal<uint32_t>{}, aabb_min) && !lb.Any(std::greater_equal<uint32_t>{}, aabb_max);
	}
	inline bool Edit(const hashdag::NodeCoord<uint32_t> &coord, bool voxel) const {
		/* if (coord.level != level)
		    printf("ERROR\n");
		if (coord.pos.All(std::greater_equal<uint32_t>{}, aabb_min) && coord.pos.All(std::less<uint32_t>{}, aabb_max))
		    printf("(%d %d %d)\n", coord.pos.x, coord.pos.y, coord.pos.z); */
		CHECK_EQ(coord.level, level);
		return coord.pos.All(std::greater_equal<uint32_t>{}, aabb_min) &&
		       coord.pos.All(std::less<uint32_t>{}, aabb_max);
	}
};

struct ZeroNodePool final : public hashdag::NodePoolBase<ZeroNodePool, uint32_t, ZeroHasher> {
	std::vector<uint32_t> memory;
	std::unordered_map<uint32_t, uint32_t> bucket_words;

	inline explicit ZeroNodePool(uint32_t level_count)
	    : hashdag::NodePoolBase<ZeroNodePool, uint32_t, ZeroHasher>(
	          hashdag::NodeConfig<uint32_t>::MakeDefault(level_count)) {
		memory.resize(GetNodeConfig().GetTotalBuckets() * GetNodeConfig().GetWordsPerBucket());
	}

	inline uint32_t GetBucketWords(uint32_t bucket_id) const {
		auto it = bucket_words.find(bucket_id);
		return it == bucket_words.end() ? 0 : it->second;
	}
	inline void SetBucketWords(uint32_t bucket_id, uint32_t words) { bucket_words[bucket_id] = words; }
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
	std::unordered_map<uint32_t, uint32_t> bucket_words;

	inline explicit MurmurNodePool(uint32_t level_count)
	    : hashdag::NodePoolBase<MurmurNodePool, uint32_t, MurmurHasher>(
	          hashdag::NodeConfig<uint32_t>::MakeDefault(level_count)) {
		memory.resize(GetNodeConfig().GetTotalWords());
	}

	inline uint32_t GetBucketWords(uint32_t bucket_id) const {
		auto it = bucket_words.find(bucket_id);
		return it == bucket_words.end() ? 0 : it->second;
	}
	inline void SetBucketWords(uint32_t bucket_id, uint32_t words) { bucket_words[bucket_id] = words; }
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
		auto ptr = pool.upsert_inner_node(0, node0, {});
		CHECK(ptr);

		auto ptr2 = pool.upsert_inner_node(0, node0, {});
		CHECK(ptr2);
		CHECK_EQ(*ptr, *ptr2);
		CHECK_EQ(pool.bucket_words[*ptr / pool.GetNodeConfig().GetWordsPerBucket()], 3);

		std::vector<uint32_t> node1 = {0b110u, 0x23, 0x44};
		auto ptr3 = pool.upsert_inner_node(0, node1, {});
		CHECK(ptr3);
		CHECK_NE(*ptr, *ptr3);

		auto ptr4 = pool.upsert_inner_node(1, node1, {});
		CHECK(ptr4);
		CHECK_NE(*ptr4, *ptr3);

		std::vector<uint32_t> node2 = {0b11111111u, 0x23, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x01};
		auto ptr5 = pool.upsert_inner_node(2, node2, {});
		CHECK(ptr5);
		CHECK_EQ(pool.bucket_words[*ptr5 / pool.GetNodeConfig().GetWordsPerBucket()], 9);

		std::array<uint32_t, 2> leaf0 = {0x23, 0x55};
		auto ptr6 = pool.upsert_leaf(3, leaf0, {});
		CHECK(ptr6);
		CHECK_EQ(pool.bucket_words[*ptr6 / pool.GetNodeConfig().GetWordsPerBucket()], 2);
	}
	TEST_CASE("Test upsert() bucket full") {
		ZeroNodePool pool(4);
		uint32_t cnt = 0;
		while (true) {
			std::vector<uint32_t> node = {0b11u, cnt, 0x45};
			auto ptr = pool.upsert_inner_node(0, node, {});
			if (!ptr)
				break;
			CHECK(((*ptr % pool.GetNodeConfig().GetWordsPerPage()) % 3 == 0));
			++cnt;
		}
		CHECK_EQ(cnt, (pool.GetNodeConfig().GetWordsPerPage() / 3) * pool.GetNodeConfig().GetPagesPerBucket());
	}
	TEST_CASE("Test Edit()") {
		MurmurNodePool pool(4);
		auto root = pool.Edit(
		    {}, AABBEditor{.level = pool.GetNodeConfig().GetLowestLevel(), .aabb_min = {}, .aabb_max{4, 4, 4}});
		CHECK(root);
		CHECK_EQ(pool.bucket_words.size(), 4);

		auto root2 = pool.Edit(
		    root, AABBEditor{.level = pool.GetNodeConfig().GetLowestLevel(), .aabb_min = {}, .aabb_max{4, 4, 4}});
		CHECK(root2);
		CHECK_EQ(root, root2);

		auto root3 = pool.Edit(
		    root2,
		    AABBEditor{.level = pool.GetNodeConfig().GetLowestLevel(), .aabb_min = {1, 1, 1}, .aabb_max{5, 5, 5}});
		CHECK(root3);
		CHECK_NE(root, root3);
	}
}

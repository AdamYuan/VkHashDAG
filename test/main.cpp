#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <hashdag/NodePool.hpp>
#include <hashdag/NodePoolThreadedEdit.hpp>

#include <unordered_map>
#include <unordered_set>

struct ZeroHasher {
	inline uint32_t operator()(auto &&) const { return 0; }
};

struct AABBEditor {
	uint32_t level;
	glm::u32vec3 aabb_min, aabb_max;
	inline hashdag::EditType EditNode(const hashdag::NodeCoord<uint32_t> &coord, hashdag::NodePointer<uint32_t>) const {
		auto lb = coord.GetLowerBoundAtLevel(level), ub = coord.GetUpperBoundAtLevel(level);
		/* printf("(%d %d %d), (%d, %d, %d) -> %d\n", lb.x, lb.y, lb.z, ub.x, ub.y, ub.z,
		       !ub.Any(std::less_equal<uint32_t>{}, aabb_min) && !lb.Any(std::greater_equal<uint32_t>{}, aabb_max)); */
		if (ub.Any(std::less_equal<uint32_t>{}, aabb_min) || lb.Any(std::greater_equal<uint32_t>{}, aabb_max))
			return hashdag::EditType::kNotAffected;
		if (lb.All(std::greater_equal<uint32_t>{}, aabb_min) && ub.All(std::less_equal<uint32_t>{}, aabb_max))
			return hashdag::EditType::kFill;
		return hashdag::EditType::kProceed;
	}
	inline bool EditVoxel(const hashdag::NodeCoord<uint32_t> &coord, bool voxel) const {
		/*if (coord.pos.All(std::greater_equal<uint32_t>{}, aabb_min) && coord.pos.All(std::less<uint32_t>{}, aabb_max))
		    printf("(%d %d %d)\n", coord.pos.x, coord.pos.y, coord.pos.z);
		*/
		return voxel || coord.pos.All(std::greater_equal<uint32_t>{}, aabb_min) &&
		                    coord.pos.All(std::less<uint32_t>{}, aabb_max);
	}
};

struct SingleIterator {
	uint32_t level;
	glm::u32vec3 pos;
	bool exist;
	inline hashdag::IterateType IterateNode(const hashdag::NodeCoord<uint32_t> &coord,
	                                        hashdag::NodePointer<uint32_t> node) const {
		auto lb = coord.GetLowerBoundAtLevel(level), ub = coord.GetUpperBoundAtLevel(level);
		if (node && !ub.Any(std::less_equal<uint32_t>{}, pos) && !lb.Any(std::greater<uint32_t>{}, pos))
			return hashdag::IterateType::kProceed;
		return hashdag::IterateType::kStop;
	}
	inline void IterateVoxel(const hashdag::NodeCoord<uint32_t> &coord, bool voxel) {
		CHECK_EQ(coord.level, level);
		if (voxel && coord.pos == pos)
			exist = true;
	}
};

struct ZeroNodePool final : public hashdag::NodePoolBase<ZeroNodePool, uint32_t> {
	using WordSpanHasher = ZeroHasher;

	std::vector<uint32_t> memory;
	std::unordered_map<uint32_t, uint32_t> bucket_words;
	std::unordered_set<uint32_t> pages;

	inline ~ZeroNodePool() final = default;
	inline explicit ZeroNodePool(uint32_t level_count)
	    : hashdag::NodePoolBase<ZeroNodePool, uint32_t>(hashdag::Config<uint32_t>::MakeDefault(level_count)) {
		memory.resize(GetConfig().GetTotalBuckets() * GetConfig().GetWordsPerBucket());
	}

	inline uint32_t GetBucketWords(uint32_t bucket_id) const {
		auto it = bucket_words.find(bucket_id);
		return it == bucket_words.end() ? 0 : it->second;
	}
	inline void SetBucketWords(uint32_t bucket_id, uint32_t words) { bucket_words[bucket_id] = words; }
	inline const uint32_t *ReadPage(uint32_t page_id) const {
		CHECK(pages.count(page_id));
		return memory.data() + (page_id << GetConfig().word_bits_per_page);
	}
	inline void ZeroPage(uint32_t page_id, uint32_t page_offset, uint32_t zero_words) { pages.insert(page_id); }
	inline void WritePage(uint32_t page_id, uint32_t page_offset, std::span<const uint32_t> word_span) {
		pages.insert(page_id);
		std::copy(word_span.begin(), word_span.end(),
		          memory.data() + ((page_id << GetConfig().word_bits_per_page) | page_offset));
	}
};
struct MurmurNodePool final : public hashdag::NodePoolBase<MurmurNodePool, uint32_t>,
                              public hashdag::NodePoolThreadedEdit<MurmurNodePool, uint32_t> {
	using WordSpanHasher = hashdag::MurmurHasher32;

	std::vector<uint32_t> memory;
	std::unordered_map<uint32_t, uint32_t> bucket_words;
	std::unordered_set<uint32_t> pages;

	hashdag::EditMutex m_edit_mutex{};

	inline ~MurmurNodePool() final = default;
	inline explicit MurmurNodePool(uint32_t level_count)
	    : hashdag::NodePoolBase<MurmurNodePool, uint32_t>(hashdag::Config<uint32_t>::MakeDefault(level_count)) {
		memory.resize(GetConfig().GetTotalWords());
	}

	inline hashdag::EditMutex &GetBucketEditMutex(uint32_t bucket_id) { return m_edit_mutex; }
	inline uint32_t GetBucketWords(uint32_t bucket_id) const {
		auto it = bucket_words.find(bucket_id);
		return it == bucket_words.end() ? 0 : it->second;
	}
	inline void SetBucketWords(uint32_t bucket_id, uint32_t words) { bucket_words[bucket_id] = words; }
	inline const uint32_t *ReadPage(uint32_t page_id) const {
		CHECK(pages.count(page_id));
		return memory.data() + (page_id << GetConfig().word_bits_per_page);
	}
	inline void ZeroPage(uint32_t page_id, uint32_t page_offset, uint32_t zero_words) { pages.insert(page_id); }
	inline void WritePage(uint32_t page_id, uint32_t page_offset, std::span<const uint32_t> word_span) {
		pages.insert(page_id);
		std::copy(word_span.begin(), word_span.end(),
		          memory.data() + ((page_id << GetConfig().word_bits_per_page) | page_offset));
	}
};

TEST_SUITE("NodePool") {
	TEST_CASE("Test upsert()") {
		MurmurNodePool pool(4);
		std::vector<uint32_t> node0 = {0b11u, 0x23, 0x45};
		auto ptr = pool.upsert_inner_node<false>(0, node0, {});
		CHECK(ptr);

		auto ptr2 = pool.upsert_inner_node<false>(0, node0, {});
		CHECK(ptr2);
		CHECK_EQ(*ptr, *ptr2);
		CHECK_EQ(pool.bucket_words[*ptr / pool.GetConfig().GetWordsPerBucket()], 3);

		std::vector<uint32_t> node1 = {0b110u, 0x23, 0x44};
		auto ptr3 = pool.upsert_inner_node<false>(0, node1, {});
		CHECK(ptr3);
		CHECK_NE(*ptr, *ptr3);

		auto ptr4 = pool.upsert_inner_node<false>(1, node1, {});
		CHECK(ptr4);
		CHECK_NE(*ptr4, *ptr3);

		std::vector<uint32_t> node2 = {0b11111111u, 0x23, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x01};
		auto ptr5 = pool.upsert_inner_node<false>(2, node2, {});
		CHECK(ptr5);
		CHECK_EQ(pool.bucket_words[*ptr5 / pool.GetConfig().GetWordsPerBucket()], 9);

		std::array<uint32_t, 2> leaf0 = {0x23, 0x55};
		auto ptr6 = pool.upsert_leaf<false>(3, leaf0, {});
		CHECK(ptr6);
		CHECK_EQ(pool.bucket_words[*ptr6 / pool.GetConfig().GetWordsPerBucket()], 2);
	}
	TEST_CASE("Test upsert() bucket full") {
		ZeroNodePool pool(4);
		uint32_t cnt = 0;
		while (true) {
			std::vector<uint32_t> node = {0b11u, cnt, 0x45};
			auto ptr = pool.upsert_inner_node<false>(0, node, {});
			if (!ptr)
				break;
			CHECK(((*ptr % pool.GetConfig().GetWordsPerPage()) % 3 == 0));
			++cnt;
		}
		CHECK_EQ(cnt, (pool.GetConfig().GetWordsPerPage() / 3) * pool.GetConfig().GetPagesPerBucket());
	}
	TEST_CASE("Test Edit() and Iterate()") {
		MurmurNodePool pool(4);
		SingleIterator iter{};
		auto root =
		    pool.Edit({}, AABBEditor{.level = pool.GetConfig().GetLowestLevel(), .aabb_min = {}, .aabb_max{4, 4, 4}});
		CHECK(root);

		auto root2 =
		    pool.Edit(root, AABBEditor{.level = pool.GetConfig().GetLowestLevel(), .aabb_min = {}, .aabb_max{4, 4, 4}});
		CHECK(root2);
		CHECK_EQ(root, root2);

		iter = SingleIterator{.level = pool.GetConfig().GetLowestLevel(), .pos = {3, 3, 3}, .exist = false};
		pool.Iterate(root2, &iter);
		CHECK(iter.exist);

		iter = SingleIterator{.level = pool.GetConfig().GetLowestLevel(), .pos = {4, 3, 3}, .exist = false};
		pool.Iterate(root2, &iter);
		CHECK(!iter.exist);

		iter = SingleIterator{.level = pool.GetConfig().GetLowestLevel(), .pos = {3, 3, 3}, .exist = false};
		pool.Iterate({}, &iter);
		CHECK(!iter.exist);

		auto root3 = pool.Edit(
		    root2, AABBEditor{.level = pool.GetConfig().GetLowestLevel(), .aabb_min = {1, 1, 1}, .aabb_max{5, 5, 5}});
		CHECK(root3);
		CHECK_NE(root, root3);

		auto root4 = pool.Edit(
		    root3, AABBEditor{.level = pool.GetConfig().GetLowestLevel(), .aabb_min = {1, 2, 3}, .aabb_max{3, 5, 5}});
		CHECK(root4);
		CHECK_EQ(root3, root4);
		CHECK_GT(pool.pages.size(), 4);

		iter = SingleIterator{.level = pool.GetConfig().GetLowestLevel(), .pos = {4, 3, 3}, .exist = false};
		pool.Iterate(root4, &iter);
		CHECK(iter.exist);
	}
	TEST_CASE("Test ThreadedEdit()") {
		lf::busy_pool busy_pool(12);

		MurmurNodePool pool(6);
		auto root = pool.ThreadedEdit(
		    &busy_pool, {},
		    AABBEditor{.level = pool.GetConfig().GetLowestLevel(), .aabb_min = {}, .aabb_max{43, 21, 3}});
		CHECK(root);
	}
}

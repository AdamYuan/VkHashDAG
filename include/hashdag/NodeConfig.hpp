//
// Created by adamyuan on 1/10/24.
//

#pragma once
#ifndef VKHASHDAG_CONFIG_HPP
#define VKHASHDAG_CONFIG_HPP

#include <cinttypes>
#include <concepts>
#include <numeric>
#include <vector>

namespace hashdag {

template <std::unsigned_integral Word> struct NodeConfig {
	inline static constexpr Word kWordsPerLeaf = sizeof(uint64_t) / sizeof(Word);
	// At least 16 words per page so that at least a node (at most 9 words) can fit into a page
	inline static constexpr Word kMinWordBitsPerPage = 4;
	Word word_bits_per_page;
	Word page_bits_per_bucket;
	std::vector<Word> bucket_bits_each_level;

	inline static constexpr Word GetWordsPerLeaf() { return kWordsPerLeaf; }
	inline Word GetWordsPerPage() const { return 1 << word_bits_per_page; }
	inline Word GetPagesPerBucket() const { return 1 << page_bits_per_bucket; }
	inline Word GetWordsPerBucket() const { return 1 << (word_bits_per_page + page_bits_per_bucket); }
	inline Word GetBucketsAtLevel(Word level) const { return 1 << bucket_bits_each_level[level]; }
	inline Word GetNodeLevels() const { return bucket_bits_each_level.size(); }
	inline Word GetActualLevels() const {
		return GetNodeLevels() + 2; // Plus 2 because leaf node is a 4^3 volume
	}
	inline Word GetTotalBuckets() const {
		Word total_buckets = 0;
		for (Word bucket_bits : bucket_bits_each_level)
			total_buckets += (1u << bucket_bits);
		return total_buckets;
	}
	inline std::vector<Word> GetLevelBaseBucketIndices() const {
		std::vector<Word> base_bucket_indices(bucket_bits_each_level.size());
		for (Word i = 1; i < bucket_bits_each_level.size(); ++i)
			base_bucket_indices[i] = GetBucketsAtLevel(i - 1) + base_bucket_indices[i - 1];
		return base_bucket_indices;
	}
	inline Word GetTotalPages() const { return GetTotalBuckets() << page_bits_per_bucket; }
	inline Word GetTotalWords() const { return GetTotalBuckets() << (word_bits_per_page + page_bits_per_bucket); }

	inline static bool Validate(const NodeConfig &config) {
		if (config.word_bits_per_page < kMinWordBitsPerPage)
			return false;
		uint64_t bucket_count = 0;
		for (Word c : config.buckets_each_level)
			bucket_count += 1ULL << c;
		uint64_t total_words = bucket_count * config.GetWordsPerBucket();
		return total_words - 1 <= uint64_t(std::numeric_limits<Word>::max());
	}
	inline static NodeConfig MakeDefault(uint32_t level_count, uint32_t top_level_count = 9,
	                                     Word word_bits_per_page = 9,           // 512
	                                     Word page_bits_per_bucket = 2,         // 4
	                                     Word bucket_bits_per_top_level = 10,   // 1024
	                                     Word bucket_bits_per_bottom_level = 16 // 65536
	) {
		std::vector<Word> bucket_bits_each_level;
		for (uint32_t l = 0; l < level_count; ++l)
			bucket_bits_each_level.push_back(l <= top_level_count ? bucket_bits_per_top_level
			                                                      : bucket_bits_per_bottom_level);
		return {.word_bits_per_page = word_bits_per_page,
		        .page_bits_per_bucket = page_bits_per_bucket,
		        .bucket_bits_each_level = std::move(bucket_bits_each_level)};
	}
};

} // namespace hashdag

#endif // VKHASHDAG_CONFIG_HPP

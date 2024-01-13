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
	// At least 16 words per page so that at least a node (at most 9 words) can fit into a page
	inline static constexpr Word kMinWordBitsPerPage = 4;
	Word word_bits_per_page;
	Word page_bits_per_bucket;
	std::vector<Word> bucket_bits_each_level;

	inline Word GetWordsPerPage() const { return 1 << word_bits_per_page; }
	inline Word GetPagesPerBucket() const { return 1 << page_bits_per_bucket; }
	inline Word GetWordsPerBucket() const { return 1 << (word_bits_per_page + page_bits_per_bucket); }
	inline Word GetBucketsAtLevel(Word level) const { return 1 << bucket_bits_each_level[level]; }
	inline Word GetLevelCount() const { return bucket_bits_each_level.size(); }

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

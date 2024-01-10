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

template <typename Word> struct Config {
	Word words_per_page;
	Word pages_per_bucket;
	std::vector<Word> buckets_each_level;

	inline Word GetWordsPerBucket() const { return pages_per_bucket * words_per_page; }
	inline auto GetLevelCount() const { return buckets_each_level.size(); }

	inline static bool Validate(const Config &config) {
		if (config.words_per_page <= 1)
			return false;
		if (config.pages_per_bucket == 0)
			return false;

		uint64_t bucket_count = 0;
		for (uint32_t c : config.buckets_each_level) {
			if (c == 0)
				return false;
			bucket_count += uint64_t(c);
		}
		uint64_t total_words = bucket_count * uint64_t(config.pages_per_bucket) * uint64_t(config.words_per_page);

		return total_words - 1 <= uint64_t(std::numeric_limits<Word>::max());
	}
	inline static Config MakeDefault(uint32_t level_count, uint32_t top_level_count = 9, Word words_per_page = 512,
	                                 Word pages_per_bucket = 4, Word buckets_per_top_level = 1024,
	                                 Word buckets_per_bottom_level = 65536) {
		std::vector<Word> buckets_each_level;
		for (uint32_t l = 0; l < level_count; ++l)
			buckets_each_level.push_back(l <= top_level_count ? buckets_per_top_level : buckets_per_bottom_level);
		return {
		    .words_per_page = words_per_page,
		    .pages_per_bucket = pages_per_bucket,
		    .buckets_each_level = std::move(buckets_each_level),
		};
	}
};

} // namespace hashdag

#endif // VKHASHDAG_CONFIG_HPP

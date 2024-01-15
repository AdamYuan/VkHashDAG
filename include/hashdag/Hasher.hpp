//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_HASHER_HPP
#define VKHASHDAG_HASHER_HPP

#include "Config.hpp"
#include <cinttypes>
#include <concepts>
#include <span>

namespace hashdag {

template <typename T, typename Word>
concept Hasher = requires(const T ce) {
	{ ce(std::span<Word>{}) } -> std::convertible_to<Word>;
};

struct MurmurHasher32 {
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

} // namespace hashdag

#endif // VKHASHDAG_HASHER_HPP

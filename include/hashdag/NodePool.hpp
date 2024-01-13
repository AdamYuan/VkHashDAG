//
// Created by adamyuan on 1/12/24.
//

#pragma once
#ifndef VKHASHDAG_NODEPOOL_HPP
#define VKHASHDAG_NODEPOOL_HPP

#include "NodeConfig.hpp"
#include <array>
#include <bit>
#include <concepts>
#include <numeric>
#include <span>

namespace hashdag {

static constexpr uint8_t kPopCount8[] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2,
    3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3,
    3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5,
    6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4,
    3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4,
    5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6,
    6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};

template <typename Derived, std::unsigned_integral Word, typename WordSpanHasher> class NodePoolBase {
private:
	NodeConfig<Word> m_config;
	std::vector<Word> m_bucket_level_bases, m_bucket_word_counts;

	inline static Word pop_count_8(uint8_t u8) { return kPopCount8[u8]; }
	inline static Word get_packed_node_hash(const Word *p_packed_node) {
		return WordSpanHasher{}(std::span<const Word>{p_packed_node, pop_count_8(*p_packed_node) + 1});
	}
	inline static Word get_leaf_hash(const Word *p_leaf) {
		return WordSpanHasher{}(std::span<const Word, sizeof(uint64_t) / sizeof(Word)>{p_leaf});
	}
	inline static void pack_node_inplace(Word *p_unpacked_node) {
		Word child_mask = *p_unpacked_node;
		Word *p_children = p_unpacked_node + 1, *p_next_child = p_children;

		while (child_mask) {
			Word child_idx = std::countr_zero(child_mask);
			child_mask ^= 1 << child_idx;
			*(p_next_child++) = p_children[child_idx];
		}
	}
	inline static std::array<Word, 9> get_unpacked_node(const Word *p_packed_node) {
		Word child_mask = *p_packed_node;
		const Word *p_next_child = p_packed_node + 1;

		std::array<Word, 9> unpacked_node = {child_mask};
		Word *p_children = unpacked_node.data() + 1;

		while (child_mask) {
			Word child_idx = std::countr_zero(child_mask);
			child_mask ^= 1 << child_idx;
			p_children[child_idx] = *(p_next_child++);
		}
		return unpacked_node;
	}
	inline static Word find_node_in_span(Word base, std::span<const Word> word_span, const Word *p_packed_node) {
		for (auto node_iter = word_span.begin(); node_iter < word_span.end();) {
			Word child_mask = *node_iter;
			if (child_mask == 0)
				return 0;
			Word child_count = pop_count_8(child_mask);

			auto child_iter = node_iter + 1;
			// Child out of range
			// TODO: this is not needed?
			// if (child_iter + child_count >= word_span.end())
			// 	return 0;

			if (child_mask == *p_packed_node && std::equal(child_iter, child_iter + child_count, p_packed_node + 1))
				return base + (node_iter - word_span.begin());

			node_iter = child_iter + child_count;
		}
	}
	inline const Word *get_page(Word page_id) const { return static_cast<const Derived *>(this)->GetPage(page_id); }

public:
	inline NodePoolBase(NodeConfig<Word> config) : m_config{std::move(config)} {
		m_bucket_level_bases.resize(m_config.GetLevelCount());
		for (Word i = 1; i < m_config.GetLevelCount(); ++i)
			m_bucket_level_bases[i] = m_config.GetBucketsAtLevel(i - 1) + m_bucket_level_bases[i - 1];

		Word total_buckets = 0;
		for (Word i = 0; i < m_config.GetLevelCount(); ++i)
			total_buckets += m_config.GetBucketsAtLevel(i);
		m_bucket_word_counts.resize(total_buckets);
	}
	inline const auto &GetNodeConfig() const { return m_config; }
	inline Word FindNode(Word level, const Word *p_packed_node) {
		Word level_bucket_index = get_packed_node_hash(p_packed_node) & (m_config.GetBucketsAtLevel(level) - 1);
		Word bucket_index = m_bucket_level_bases[level] + level_bucket_index;
		Word bucket_word_count = m_bucket_word_counts[bucket_index];
		Word page_index = bucket_index << m_config.page_bits_per_bucket;

		while (bucket_word_count >= m_config.GetWordsPerPage()) {
			Word node = find_node_in_span(page_index << m_config.word_bits_per_page,
			                              std::span<const Word>{get_page(page_index), m_config.GetWordsPerPage()},
			                              p_packed_node);
			if (node)
				return node;
			bucket_word_count -= m_config.GetWordsPerPage();
			++page_index;
		}
		return bucket_word_count
		           ? find_node_in_span(page_index << m_config.word_bits_per_page,
		                               std::span<const Word>{get_page(page_index), bucket_word_count}, p_packed_node)
		           : 0;
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOL_HPP

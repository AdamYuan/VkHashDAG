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
#include <optional>
#include <span>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word, typename WordSpanHasher> class NodePoolBase {
#ifndef HASHDAG_TEST
private:
#else
public:
#endif
	NodeConfig<Word> m_config;
	std::vector<Word> m_bucket_level_bases, m_bucket_word_counts;

	inline static std::span<const Word> get_packed_node_inplace(Word *p_unpacked_node) {
		Word child_mask = *p_unpacked_node;
		Word *p_children = p_unpacked_node + 1, *p_next_child = p_children;

		while (child_mask) {
			Word child_idx = std::countr_zero(child_mask);
			child_mask ^= 1 << child_idx;
			*(p_next_child++) = p_children[child_idx];
		}
		return std::span<const Word>{p_unpacked_node, p_next_child};
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
	inline const Word *read_page(Word page_id) const { return static_cast<const Derived *>(this)->ReadPage(page_id); }
	inline void zero_page(Word page_id, Word page_offset, Word zero_words) {
		static_cast<Derived *>(this)->ZeroPage(page_id, page_offset, zero_words);
	}
	inline void write_page(Word page_id, Word page_offset, std::span<const Word> word_span) {
		static_cast<Derived *>(this)->WritePage(page_id, page_offset, word_span);
	}

	template <size_t NodeSpanExtent>
	inline static std::optional<Word> find_node_in_span(auto &&get_node_words, Word base,
	                                                    std::span<const Word> word_span,
	                                                    std::span<const Word, NodeSpanExtent> node_span) {
		for (auto iter = word_span.begin(); iter + node_span.size() <= word_span.end();) {
			Word node_words = get_node_words(&(*iter));
			if (node_words == 0)
				break;
			if (node_words == node_span.size() && std::equal(node_span.begin(), node_span.end(), iter))
				return base + (iter - word_span.begin());
			iter += node_words;
		}
		return std::nullopt;
	}
	template <size_t NodeSpanExtent>
	inline std::optional<Word> upsert_node(auto &&get_node_words, Word level,
	                                       std::span<const Word, NodeSpanExtent> node_span) {
		const Word bucket_index =
		    m_bucket_level_bases[level] + (WordSpanHasher{}(node_span) & (m_config.GetBucketsAtLevel(level) - 1));
		const Word bucket_words = m_bucket_word_counts[bucket_index];
		const Word bucket_page_index = bucket_index << m_config.page_bits_per_bucket;

		// Find Node in the bucket
		{
			Word cur_bucket_words = bucket_words;
			Word cur_page_index = bucket_page_index;

			while (cur_bucket_words >= m_config.GetWordsPerPage()) {
				std::optional<Word> opt_node = find_node_in_span(
				    get_node_words, cur_page_index << m_config.word_bits_per_page,
				    std::span<const Word>{read_page(cur_page_index), m_config.GetWordsPerPage()}, node_span);
				if (opt_node)
					return opt_node;
				cur_bucket_words -= m_config.GetWordsPerPage();
				++cur_page_index;
			}
			if (cur_bucket_words) {
				std::optional<Word> opt_node =
				    find_node_in_span(get_node_words, cur_page_index << m_config.word_bits_per_page,
				                      std::span<const Word>{read_page(cur_page_index), cur_bucket_words}, node_span);
				if (opt_node)
					return opt_node;
			}
		}

		// Append Node if not exist
		{
			// If the bucket is full, return std::nullopt
			if (bucket_words + node_span.size() > m_config.GetWordsPerBucket())
				return std::nullopt;

			Word dst_page_slot = bucket_words >> m_config.word_bits_per_page; // PageID in bucket
			Word dst_page_index = bucket_page_index | dst_page_slot;
			Word dst_page_offset = bucket_words & (m_config.GetWordsPerPage() - 1);
			if (dst_page_offset + node_span.size() > m_config.GetWordsPerPage()) {
				// Fill the remaining with zero
				zero_page(dst_page_index, dst_page_offset, m_config.GetWordsPerPage() - dst_page_offset);
				// Write node to next page
				++dst_page_slot;
				++dst_page_index;
				dst_page_offset = 0;
			}

			write_page(dst_page_index, dst_page_offset, node_span);
			m_bucket_word_counts[bucket_index] =
			    ((dst_page_slot << m_config.word_bits_per_page) | dst_page_offset) + node_span.size();

			return (dst_page_index << m_config.word_bits_per_page) | dst_page_offset;
		}
	}

public:
	inline explicit NodePoolBase(NodeConfig<Word> config) : m_config{std::move(config)} {
		m_bucket_word_counts.resize(m_config.GetTotalBuckets());

		m_bucket_level_bases.resize(m_config.GetLevelCount());
		for (Word i = 1; i < m_config.GetLevelCount(); ++i)
			m_bucket_level_bases[i] = m_config.GetBucketsAtLevel(i - 1) + m_bucket_level_bases[i - 1];
	}
	inline const auto &GetNodeConfig() const { return m_config; }

	inline std::optional<Word> UpsertNode(Word level, std::span<const Word> node_span) {
		const auto get_node_words = [](const Word *p_packed_node) {
			// Zero for empty node so that find_node can break
			static constexpr uint8_t kPopCount8_Plus1_Zero[] = {
			    0, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
			    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
			    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
			    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
			    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
			    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
			    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
			    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, 5, 6, 6, 7, 6, 7, 7, 8, 6, 7, 7, 8, 7, 8, 8, 9,
			};
			return kPopCount8_Plus1_Zero[uint8_t(*p_packed_node)];
		};
		return upsert_node(get_node_words, level, node_span);
	}
	inline std::optional<Word> UpsertLeaf(Word level,
	                                      std::span<const Word, NodeConfig<Word>::kWordsPerLeaf> leaf_span) {
		const auto get_node_words = [](auto) { return NodeConfig<Word>::kWordsPerLeaf; };
		return upsert_node(get_node_words, level, leaf_span);
	}
	template <typename Editor> inline Word Edit() {}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOL_HPP

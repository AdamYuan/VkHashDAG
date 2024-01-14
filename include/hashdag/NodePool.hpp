//
// Created by adamyuan on 1/12/24.
//

#pragma once
#ifndef VKHASHDAG_NODEPOOL_HPP
#define VKHASHDAG_NODEPOOL_HPP

#include "Editor.hpp"
#include "NodeConfig.hpp"
#include "Position.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <span>

namespace hashdag {

template <std::unsigned_integral Word> class NodePointer {
private:
	Word m_node;

public:
	inline NodePointer() : m_node(-1) {}
	inline NodePointer(Word node) : m_node{node} {}

	inline bool HasValue() const { return m_node != -1; }
	inline operator bool() const { return HasValue(); }

	inline Word Value() const { return m_node; }
	inline Word operator*() const { return Value(); }

	inline static NodePointer Null() { return {}; }
};

template <typename Derived, std::unsigned_integral Word, typename WordSpanHasher> class NodePoolBase {
#ifndef HASHDAG_TEST
private:
#else
public:
#endif
	NodeConfig<Word> m_config;
	std::vector<Word> m_bucket_level_bases, m_bucket_word_counts;

	inline const Word *read_page(Word page_id) const { return static_cast<const Derived *>(this)->ReadPage(page_id); }
	inline const Word *read_node(Word node) const {
		return read_page(node >> m_config.word_bits_per_page) + (node & (m_config.GetWordsPerPage() - 1u));
	}
	inline void zero_page(Word page_id, Word page_offset, Word zero_words) {
		static_cast<Derived *>(this)->ZeroPage(page_id, page_offset, zero_words);
	}
	inline void write_page(Word page_id, Word page_offset, std::span<const Word> word_span) {
		static_cast<Derived *>(this)->WritePage(page_id, page_offset, word_span);
	}

	template <size_t NodeSpanExtent>
	inline static NodePointer<Word> find_node_in_span(auto &&get_node_words, Word base, std::span<const Word> word_span,
	                                                  std::span<const Word, NodeSpanExtent> node_span) {
		for (auto iter = word_span.begin(); iter + node_span.size() <= word_span.end();) {
			Word node_words = get_node_words(&(*iter));
			if (node_words == 0)
				break;
			if (node_words == node_span.size() && std::equal(node_span.begin(), node_span.end(), iter))
				return base + (iter - word_span.begin());
			iter += node_words;
		}
		return NodePointer<Word>::Null();
	}
	template <size_t NodeSpanExtent>
	inline NodePointer<Word> upsert_node(auto &&get_node_words, Word level,
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
				NodePointer<Word> node_ptr = find_node_in_span(
				    get_node_words, cur_page_index << m_config.word_bits_per_page,
				    std::span<const Word>{read_page(cur_page_index), m_config.GetWordsPerPage()}, node_span);
				if (node_ptr)
					return node_ptr;
				cur_bucket_words -= m_config.GetWordsPerPage();
				++cur_page_index;
			}
			if (cur_bucket_words) {
				NodePointer<Word> node_ptr =
				    find_node_in_span(get_node_words, cur_page_index << m_config.word_bits_per_page,
				                      std::span<const Word>{read_page(cur_page_index), cur_bucket_words}, node_span);
				if (node_ptr)
					return node_ptr;
			}
		}

		// Append Node if not exist
		{
			// If the bucket is full, return Null
			if (bucket_words + node_span.size() > m_config.GetWordsPerBucket())
				return NodePointer<Word>::Null();

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

	inline NodePointer<Word> upsert_inner_node(Word level, std::span<const Word> node_span) {
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
	inline NodePointer<Word> upsert_leaf(Word level, std::span<const Word, NodeConfig<Word>::kWordsPerLeaf> leaf_span) {
		const auto get_node_words = [](auto) { return NodeConfig<Word>::kWordsPerLeaf; };
		return upsert_node(get_node_words, level, leaf_span);
	}

	inline static std::span<const Word> get_packed_node_inplace(std::span<Word, 9> unpacked_node) {
		Word child_mask = unpacked_node.data();
		Word *p_children = unpacked_node.data() + 1, *p_next_child = p_children;

		while (child_mask) {
			Word child_idx = std::countr_zero(child_mask);
			child_mask ^= 1 << child_idx;
			*(p_next_child++) = p_children[child_idx];
		}
		return std::span<const Word>{unpacked_node.data(), p_next_child};
	}
	inline static std::array<Word, 9> get_unpacked_node(const Word *p_node) {
		Word child_mask = *p_node;
		const Word *p_next_child = p_node + 1;

		std::array<Word, 9> unpacked_node = {child_mask};
		Word *p_children = unpacked_node.data() + 1;

		while (child_mask) {
			Word child_idx = std::countr_zero(child_mask);
			child_mask ^= 1 << child_idx;
			p_children[child_idx] = *(p_next_child++);
		}
		return unpacked_node;
	}
	inline NodePointer<Word> edit_leaf(const Editor<Word> auto &editor, NodePointer<Word> node_ptr,
	                                   const NodeCoord<Word> &coord) {
		return NodePointer<Word>::Null();
	}
	inline NodePointer<Word> edit_inner_node(const Editor<Word> auto &editor, NodePointer<Word> node_ptr,
	                                         const NodeCoord<Word> &coord) {
		if (!editor.IsAffected(coord))
			return node_ptr;

		if (coord.level == m_config.GetNodeLevels() - 1)
			return edit_leaf(editor, node_ptr, coord);

		std::array<Word, 9> unpacked_node = node_ptr ? get_unpacked_node(read_node(*node_ptr)) : std::array<Word, 9>{};
		Word &child_mask = unpacked_node[0];
		std::span<Word, 8> children = {unpacked_node.data() + 1};

		for (Word i = 0; i < 8; ++i) {
			NodePointer<Word> child_ptr = ((child_mask >> i) & 1u) ? children[i] : NodePointer<Word>::Null();
			NodePointer<Word> new_child_ptr = edit(editor, child_ptr, coord.GetChildCoord(i));

			if (new_child_ptr != child_ptr) {
				children[i] = *new_child_ptr;
				if (new_child_ptr)
					child_mask |= (1u << i);
				else
					child_mask &= ~(1u << i);
			}
		}
		return upsert_inner_node(coord.level, get_packed_node_inplace(unpacked_node));
	}

public:
	inline explicit NodePoolBase(NodeConfig<Word> config) : m_config{std::move(config)} {
		m_bucket_word_counts.resize(m_config.GetTotalBuckets());

		m_bucket_level_bases.resize(m_config.GetNodeLevels());
		for (Word i = 1; i < m_config.GetNodeLevels(); ++i)
			m_bucket_level_bases[i] = m_config.GetBucketsAtLevel(i - 1) + m_bucket_level_bases[i - 1];
	}
	inline const auto &GetNodeConfig() const { return m_config; }
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOL_HPP

//
// Created by adamyuan on 1/12/24.
//

#pragma once
#ifndef VKHASHDAG_HASHDAG_NODEPOOL_HPP
#define VKHASHDAG_HASHDAG_NODEPOOL_HPP

#include "Config.hpp"
#include "Editor.hpp"
#include "Hasher.hpp"
#include "Iterator.hpp"
#include "NodeCoord.hpp"
#include "NodePointer.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <span>

namespace hashdag {

template <typename T, typename Word>
concept NodePool = requires(T e, const T ce) {
	{ ce.ReadPage(Word{} /* Page Index */) } -> std::convertible_to<const Word *>;
	e.WritePage(Word{} /* Page Index */, Word{} /* Offset */, std::span<const Word>{} /* Content */);
	e.ZeroPage(Word{} /* Page Index */, Word{} /* Offset */, Word{} /* Length */);

	{ ce.GetBucketWords(Word{} /* Bucket Index */) } -> std::convertible_to<Word>;
	e.SetBucketWords(Word{} /* Bucket Index */, Word{} /* Words */);
};

template <typename Derived, std::unsigned_integral Word, Hasher<Word> WordSpanHasher> class NodePoolBase {
#ifndef HASHDAG_TEST
private:
#else
public:
#endif
	Config<Word> m_config;
	std::vector<Word> m_bucket_level_bases;

	inline const Word *read_page(Word page_id) const { return static_cast<const Derived *>(this)->ReadPage(page_id); }
	inline void zero_page(Word page_id, Word page_offset, Word zero_words) {
		static_cast<Derived *>(this)->ZeroPage(page_id, page_offset, zero_words);
	}
	inline void write_page(Word page_id, Word page_offset, std::span<const Word> word_span) {
		static_cast<Derived *>(this)->WritePage(page_id, page_offset, word_span);
	}
	inline Word get_bucket_words(Word bucket_id) const {
		return static_cast<const Derived *>(this)->GetBucketWords(bucket_id);
	}
	inline void set_bucket_words(Word bucket_id, Word words) {
		return static_cast<Derived *>(this)->SetBucketWords(bucket_id, words);
	}

	inline const Word *read_node(Word node) const {
		return read_page(node >> m_config.word_bits_per_page) + (node & (m_config.GetWordsPerPage() - 1u));
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
	                                     std::span<const Word, NodeSpanExtent> node_span,
	                                     NodePointer<Word> fallback_ptr) {
		const Word bucket_index =
		    m_bucket_level_bases[level] + (WordSpanHasher{}(node_span) & (m_config.GetBucketsAtLevel(level) - 1));
		const Word bucket_words = get_bucket_words(bucket_index);
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
			// If the bucket is full, return fallback
			if (bucket_words + node_span.size() > m_config.GetWordsPerBucket())
				return fallback_ptr;

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
			set_bucket_words(bucket_index,
			                 ((dst_page_slot << m_config.word_bits_per_page) | dst_page_offset) + node_span.size());

			return (dst_page_index << m_config.word_bits_per_page) | dst_page_offset;
		}
	}

	inline NodePointer<Word> upsert_inner_node(Word level, std::span<const Word> node_span,
	                                           NodePointer<Word> fallback_ptr) {
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
		return upsert_node(get_node_words, level, node_span, fallback_ptr);
	}
	inline NodePointer<Word> upsert_leaf(Word level, std::span<const Word, Config<Word>::kWordsPerLeaf> leaf_span,
	                                     NodePointer<Word> fallback_ptr) {
		const auto get_node_words = [](auto) { return Config<Word>::kWordsPerLeaf; };
		return upsert_node(get_node_words, level, leaf_span, fallback_ptr);
	}

	inline static void foreach_child_index(Word child_mask, auto &&func) {
		while (child_mask) {
			Word child_idx = std::countr_zero(child_mask);
			child_mask ^= 1 << child_idx;
			func(child_idx);
		}
	}

	inline static std::span<const Word> get_packed_node_inplace(std::span<Word, 9> unpacked_node) {
		Word child_mask = unpacked_node.front();
		Word *p_children = unpacked_node.data() + 1, *p_next_child = p_children;
		foreach_child_index(child_mask, [&](Word child_idx) { *(p_next_child++) = p_children[child_idx]; });
		return std::span<const Word>{unpacked_node.data(), p_next_child};
	}
	inline std::array<Word, 9> get_unpacked_node_array(NodePointer<Word> node_ptr) const {
		if (!node_ptr)
			return {};
		const Word *p_node = read_node(*node_ptr);
		Word child_mask = *p_node;
		const Word *p_next_child = p_node + 1;

		std::array<Word, 9> unpacked_node = {child_mask};
		Word *p_children = unpacked_node.data() + 1;

		foreach_child_index(child_mask, [&](Word child_idx) { p_children[child_idx] = *(p_next_child++); });

		return unpacked_node;
	}
	inline std::array<Word, Config<Word>::kWordsPerLeaf> get_leaf_array(NodePointer<Word> leaf_ptr) const {
		if (!leaf_ptr)
			return {};
		const Word *p_leaf = read_node(*leaf_ptr);
		std::array<Word, Config<Word>::kWordsPerLeaf> leaf;
		std::copy(p_leaf, p_leaf + Config<Word>::kWordsPerLeaf, leaf.data());
		return leaf;
	}
	inline NodePointer<Word> edit_leaf(const Editor<Word> auto &editor, NodePointer<Word> leaf_ptr,
	                                   const NodeCoord<Word> &coord) {
		using LeafArray = std::array<Word, Config<Word>::kWordsPerLeaf>;

		LeafArray leaf = get_leaf_array(leaf_ptr);

		bool changed = false;

		for (Word i = 0; i < 64; ++i) {
			constexpr Word kWordBits = std::countr_zero(sizeof(Word) * 8), kWordMask = (1u << kWordBits) - 1u;

			bool voxel = (leaf[i >> kWordBits] >> (i & kWordMask)) & 1u;
			bool new_voxel = editor.Edit(coord.GetLeafCoord(i), voxel);

			if (new_voxel != voxel) {
				changed = true;
				leaf[i >> kWordBits] ^= (1u << (i & kWordMask)); // Flip
			}
		}

		return changed ? (leaf == LeafArray{0} ? NodePointer<Word>::Null() : upsert_leaf(coord.level, leaf, leaf_ptr))
		               : leaf_ptr;
	}
	inline NodePointer<Word> edit_inner_node(const Editor<Word> auto &editor, NodePointer<Word> node_ptr,
	                                         const NodeCoord<Word> &coord) {
		if (!editor.IsAffected(coord))
			return node_ptr;

		if (coord.level == m_config.GetNodeLevels() - 1)
			return edit_leaf(editor, node_ptr, coord);

		std::array<Word, 9> unpacked_node = get_unpacked_node_array(node_ptr);
		Word &child_mask = unpacked_node[0];
		std::span<Word, 8> children = std::span<Word, 9>{unpacked_node}.template subspan<1>();

		bool changed = false;

		for (Word i = 0; i < 8; ++i) {
			NodePointer<Word> child_ptr =
			    ((child_mask >> i) & 1u) ? NodePointer<Word>{children[i]} : NodePointer<Word>::Null();
			NodePointer<Word> new_child_ptr = edit_inner_node(editor, child_ptr, coord.GetChildCoord(i));

			if (new_child_ptr != child_ptr) {
				changed = true;

				children[i] = *new_child_ptr;
				child_mask ^= (Word{bool(new_child_ptr) != bool(child_ptr)} << i); // Flip if occurrence changed
			}
		}

		return changed ? (child_mask ? upsert_inner_node(coord.level, get_packed_node_inplace(unpacked_node), node_ptr)
		                             : NodePointer<Word>::Null())
		               : node_ptr;
	}

	inline void iterate_leaf(Iterator<Word> auto *p_iterator, NodePointer<Word> leaf_ptr,
	                         const NodeCoord<Word> &coord) const {
		if (!leaf_ptr)
			return;
		const Word *p_leaf = read_node(*leaf_ptr);
		for (Word i = 0; i < 64; ++i) {
			constexpr Word kWordBits = std::countr_zero(sizeof(Word) * 8), kWordMask = (1u << kWordBits) - 1u;
			bool voxel = (p_leaf[i >> kWordBits] >> (i & kWordMask)) & 1u;
			if (voxel)
				p_iterator->Iterate(coord.GetLeafCoord(i));
		}
	}
	inline void iterate_inner_node(Iterator<Word> auto *p_iterator, NodePointer<Word> node_ptr,
	                               const NodeCoord<Word> &coord) const {
		if (!node_ptr || !p_iterator->IsAffected(coord))
			return;

		if (coord.level == m_config.GetNodeLevels() - 1) {
			iterate_leaf(p_iterator, node_ptr, coord);
			return;
		}

		const Word *p_node = read_node(*node_ptr);
		Word child_mask = *p_node;
		const Word *p_next_child = p_node + 1;

		foreach_child_index(child_mask, [&](Word child_idx) {
			iterate_inner_node(p_iterator, *(p_next_child++), coord.GetChildCoord(child_idx));
		});
	}

public:
	inline virtual ~NodePoolBase() = default;
	inline explicit NodePoolBase(Config<Word> config) : m_config{std::move(config)} {
		static_assert(NodePool<Derived, Word>);
		m_bucket_level_bases = m_config.GetLevelBaseBucketIndices();
	}
	inline const auto &GetConfig() const { return m_config; }
	inline NodePointer<Word> Edit(NodePointer<Word> root_ptr, const Editor<Word> auto &editor) {
		return edit_inner_node(editor, root_ptr, {});
	}
	inline void Iterate(NodePointer<Word> root_ptr, Iterator<Word> auto *p_iterator) const {
		iterate_inner_node(p_iterator, root_ptr, {});
	}
};

} // namespace hashdag

#endif // VKHASHDAG_HASHDAG_NODEPOOL_HPP

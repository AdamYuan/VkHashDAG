//
// Created by adamyuan on 1/12/24.
//

#pragma once
#ifndef VKHASHDAG_HASHDAG_NODEPOOL_HPP
#define VKHASHDAG_HASHDAG_NODEPOOL_HPP

#include "Editor.hpp"
#include "Hasher.hpp"
#include "NodeCoord.hpp"
#include "NodePointer.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <future>
#include <span>

namespace hashdag {

template <typename T, typename Word>
concept NodePool = Hasher<typename T::WordSpanHasher, Word> && requires(T e, const T ce) {
	{ ce.ReadPage(Word{} /* Page Index */) } -> std::convertible_to<const Word *>;
	e.WritePage(Word{} /* Page Index */, Word{} /* Offset */, std::span<const Word>{} /* Content */);
	e.ZeroPage(Word{} /* Page Index */, Word{} /* Offset */, Word{} /* Length */);

	{ e.GetBucketRefWords(Word{} /* Bucket Index */) } -> std::convertible_to<Word &>;
} && std::unsigned_integral<Word>;

template <typename T, typename Word>
concept ThreadedNodePool = NodePool<T, Word> && requires(T e, const T ce) {
	e.GetBucketRefMutex(Word{} /* Bucket Index */).lock();
	e.GetBucketRefMutex(Word{} /* Bucket Index */).unlock();
};

template <typename T, typename Word>
concept GCNodePool = NodePool<T, Word> && requires(T e) { e.FreePage(Word{} /* Page Index */); };

template <typename Derived, std::unsigned_integral Word> class NodePoolBase {
#ifndef HASHDAG_TEST
private:
#else
public:
#endif

	template <typename, std::unsigned_integral> friend class NodePoolTraversal;
	template <typename, std::unsigned_integral> friend class NodePoolThreadedEdit;
	template <typename, std::unsigned_integral, template <typename, typename> typename, template <typename> typename>
	friend class NodePoolThreadedGC;

	Config<Word> m_config;
	std::vector<Word> m_bucket_level_bases;
	std::vector<NodePointer<Word>> m_filled_node_pointers; // Should be preserved when GC

	inline const Word *read_page(Word page_id) const { return static_cast<const Derived *>(this)->ReadPage(page_id); }
	inline void zero_page(Word page_id, Word page_offset, Word zero_words) {
		static_cast<Derived *>(this)->ZeroPage(page_id, page_offset, zero_words);
	}
	inline void write_page(Word page_id, Word page_offset, std::span<const Word> word_span) {
		static_cast<Derived *>(this)->WritePage(page_id, page_offset, word_span);
	}
	inline void free_page(Word page_id) {
		static_assert(GCNodePool<Derived, Word>);
		static_cast<Derived *>(this)->FreePage(page_id);
	}
	inline auto &get_bucket_ref_mutex(Word bucket_id) {
		static_assert(ThreadedNodePool<Derived, Word>);
		return static_cast<Derived *>(this)->GetBucketRefMutex(bucket_id);
	}
	inline Word &get_bucket_ref_words(Word bucket_id) {
		return static_cast<Derived *>(this)->GetBucketRefWords(bucket_id);
	}

	inline const Word *read_node(Word node) const {
		return read_page(node >> m_config.word_bits_per_page) + (node & (m_config.GetWordsPerPage() - 1u));
	}

	template <size_t NodeSpanExtent>
	inline static NodePointer<Word> find_node_in_span(auto &&get_node_words, Word base, std::span<const Word> word_span,
	                                                  std::span<const Word, NodeSpanExtent> node_span) {
		for (auto iter = word_span.begin(); node_span.size() <= word_span.end() - iter;) {
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
	inline NodePointer<Word> find_node(auto &&get_node_words, Word bucket_index, Word bucket_words,
	                                   Word bucket_word_offset, std::span<const Word, NodeSpanExtent> node_span) {
		// Calculate the words to find among
		if (bucket_words <= bucket_word_offset)
			return NodePointer<Word>::Null();
		Word find_words = bucket_words - bucket_word_offset;

		// Page index to find
		Word page_index =
		    (bucket_index << m_config.page_bits_per_bucket) | (bucket_word_offset >> m_config.word_bits_per_page);

		// Word offset in page
		Word page_word_offset = bucket_word_offset & (m_config.GetWordsPerPage() - 1u);

		// While span reaches the end of a page
		while (page_word_offset + find_words >= m_config.GetWordsPerPage()) {
			NodePointer<Word> node_ptr =
			    find_node_in_span(get_node_words, (page_index << m_config.word_bits_per_page) | page_word_offset,
			                      std::span<const Word>{read_page(page_index) + page_word_offset,
			                                            m_config.GetWordsPerPage() - page_word_offset},
			                      node_span);
			if (node_ptr)
				return node_ptr;

			find_words -= m_config.GetWordsPerPage() - page_word_offset;
			page_word_offset = 0;
			++page_index;
		}

		// Still have words remain
		if (find_words) {
			NodePointer<Word> node_ptr = find_node_in_span(
			    get_node_words, (page_index << m_config.word_bits_per_page) | page_word_offset,
			    std::span<const Word>{read_page(page_index) + page_word_offset, find_words}, node_span);
			if (node_ptr)
				return node_ptr;
		}
		return NodePointer<Word>::Null();
	}

	template <size_t NodeSpanExtent>
	inline std::tuple<NodePointer<Word>, Word> append_node(Word bucket_index, Word bucket_words,
	                                                       std::span<const Word, NodeSpanExtent> node_span) {
		// If the bucket is full, return Null
		if (bucket_words + node_span.size() > m_config.GetWordsPerBucket())
			return {NodePointer<Word>::Null(), 0u};

		Word dst_page_slot = bucket_words >> m_config.word_bits_per_page; // PageID in bucket
		Word dst_page_offset = bucket_words & (m_config.GetWordsPerPage() - 1);
		Word dst_page_index = (bucket_index << m_config.page_bits_per_bucket) | dst_page_slot;

		if (dst_page_offset + node_span.size() > m_config.GetWordsPerPage()) {
			// Fill the remaining with zero
			zero_page(dst_page_index, dst_page_offset, m_config.GetWordsPerPage() - dst_page_offset);
			// Write node to next page
			++dst_page_slot;
			++dst_page_index;
			dst_page_offset = 0;
		}

		write_page(dst_page_index, dst_page_offset, node_span);
		Word new_bucket_words = ((dst_page_slot << m_config.word_bits_per_page) | dst_page_offset) + node_span.size();
		return {(dst_page_index << m_config.word_bits_per_page) | dst_page_offset, new_bucket_words};
	}

	template <bool ThreadSafe, size_t NodeSpanExtent>
	inline NodePointer<Word> upsert_node(auto &&get_node_words, Word level,
	                                     std::span<const Word, NodeSpanExtent> node_span,
	                                     NodePointer<Word> fallback_ptr) {
		const Word bucket_index = m_bucket_level_bases[level] + (typename Derived::WordSpanHasher{}(node_span) &
		                                                         (m_config.GetBucketsAtLevel(level) - 1));

		Word &ref_bucket_words = get_bucket_ref_words(bucket_index);

		if constexpr (ThreadSafe) {
			static_assert(std::atomic_ref<Word>::is_always_lock_free &&
			              std::atomic_ref<Word>::required_alignment == alignof(Word));

			std::atomic_ref<Word> atomic_ref_bucket_words{ref_bucket_words};
			Word shared_bucket_words = atomic_ref_bucket_words.load(std::memory_order_acquire); // Acquire
			{
				NodePointer<Word> find_node_ptr =
				    find_node(get_node_words, bucket_index, shared_bucket_words, 0, node_span);
				if (find_node_ptr)
					return find_node_ptr;
			}

			{
				std::unique_lock unique_lock{get_bucket_ref_mutex(bucket_index)}; // Acquire

				Word unique_bucket_words = atomic_ref_bucket_words.load(std::memory_order_relaxed);
				NodePointer<Word> find_node_ptr =
				    find_node(get_node_words, bucket_index, unique_bucket_words, shared_bucket_words, node_span);
				if (find_node_ptr)
					return find_node_ptr;

				auto [append_node_ptr, new_bucket_words] = append_node(bucket_index, unique_bucket_words, node_span);
				if (append_node_ptr) {
					atomic_ref_bucket_words.store(new_bucket_words, std::memory_order_relaxed);
					return append_node_ptr;
				}
				return fallback_ptr;

				// unlock; Release
			}
		} else {
			const Word bucket_words = ref_bucket_words;
			NodePointer<Word> find_node_ptr = find_node(get_node_words, bucket_index, bucket_words, 0, node_span);
			if (find_node_ptr)
				return find_node_ptr;

			auto [append_node_ptr, new_bucket_words] = append_node(bucket_index, bucket_words, node_span);
			if (append_node_ptr) {
				ref_bucket_words = new_bucket_words;
				return append_node_ptr;
			}
			return fallback_ptr;
		}
	}

	inline static Word get_inner_node_words(const Word *p_packed_node) {
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
	}

	template <bool ThreadSafe>
	inline NodePointer<Word> upsert_inner_node(Word level, std::span<const Word> node_span,
	                                           NodePointer<Word> fallback_ptr) {
		return upsert_node<ThreadSafe>(get_inner_node_words, level, node_span, fallback_ptr);
	}
	template <bool ThreadSafe>
	inline NodePointer<Word> upsert_leaf(Word level, std::span<const Word, Config<Word>::kWordsPerLeaf> leaf_span,
	                                     NodePointer<Word> fallback_ptr) {
		const auto get_node_words = [](auto) { return Config<Word>::kWordsPerLeaf; };
		return upsert_node<ThreadSafe>(get_node_words, level, leaf_span, fallback_ptr);
	}

	inline void make_filled_node_pointers() {
		if (!m_filled_node_pointers.empty())
			return;

		m_filled_node_pointers.resize(m_config.GetNodeLevels());

		{
			std::array<Word, Config<Word>::kWordsPerLeaf> leaf;
			std::fill(leaf.begin(), leaf.end(), Word(-1));
			m_filled_node_pointers.back() =
			    upsert_leaf<false>(m_config.GetNodeLevels() - 1, leaf, NodePointer<Word>::Null());
		}

		if (m_config.GetNodeLevels() == 1)
			return;

		for (Word l = m_config.GetNodeLevels() - 2u; ~l; --l) {
			Word prev_node = *m_filled_node_pointers[l + 1];
			std::array<Word, 9> node = {0xFFu,     prev_node, prev_node, prev_node, prev_node,
			                            prev_node, prev_node, prev_node, prev_node};
			m_filled_node_pointers[l] = upsert_inner_node<false>(l, node, NodePointer<Word>::Null());
		}
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
			return {0,
			        *NodePointer<Word>::Null(),
			        *NodePointer<Word>::Null(),
			        *NodePointer<Word>::Null(),
			        *NodePointer<Word>::Null(),
			        *NodePointer<Word>::Null(),
			        *NodePointer<Word>::Null(),
			        *NodePointer<Word>::Null(),
			        *NodePointer<Word>::Null()};

		const Word *p_node = read_node(*node_ptr);
		Word child_mask = *p_node;
		const Word *p_next_child = p_node + 1;

		std::array<Word, 9> unpacked_node = {
		    child_mask,
		    *NodePointer<Word>::Null(),
		    *NodePointer<Word>::Null(),
		    *NodePointer<Word>::Null(),
		    *NodePointer<Word>::Null(),
		    *NodePointer<Word>::Null(),
		    *NodePointer<Word>::Null(),
		    *NodePointer<Word>::Null(),
		    *NodePointer<Word>::Null(),
		};
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

	template <bool ThreadSafe>
	inline NodePointer<Word> edit_leaf(const Editor<Word> auto &editor, NodePointer<Word> leaf_ptr,
	                                   const NodeCoord<Word> &coord, auto &state) {
		using LeafArray = std::array<Word, Config<Word>::kWordsPerLeaf>;

		LeafArray leaf = get_leaf_array(leaf_ptr);

		bool changed = false;

		for (Word i = 0; i < 64; ++i) {
			constexpr Word kWordBits = std::countr_zero(sizeof(Word) * 8), kWordMask = (1u << kWordBits) - 1u;

			bool voxel = (leaf[i >> kWordBits] >> (i & kWordMask)) & 1u;
			bool new_voxel = editor.EditVoxel(m_config, coord.GetLeafCoord(i), voxel, state);

			changed |= new_voxel != voxel;
			leaf[i >> kWordBits] ^= (Word(new_voxel != voxel) << (i & kWordMask));
		}

		editor.JoinLeaf(m_config, coord, state);

		return changed ? (leaf == LeafArray{0} ? NodePointer<Word>::Null()
		                                       : upsert_leaf<ThreadSafe>(coord.level, leaf, leaf_ptr))
		               : leaf_ptr;
	}

	template <Editor<Word> Editor_T>
	inline auto edit_switch(const Editor_T &editor, NodePointer<Word> node_ptr, const NodeCoord<Word> &coord,
	                        auto &state, const auto &parent_state, auto &&edit_terminate_func,
	                        auto &&edit_proceed_func) const {
		auto edit_type = editor.EditNode(m_config, coord, node_ptr, state, parent_state);
		if (edit_type == EditType::kClear)
			return edit_terminate_func(NodePointer<Word>::Null());
		else if (edit_type == EditType::kFill)
			return edit_terminate_func(m_filled_node_pointers[coord.level]);
		else if (edit_type == EditType::kNotAffected)
			return edit_terminate_func(node_ptr);
		else {
			// kProceed
			return edit_proceed_func();
		}
	}

	template <bool ThreadSafe, Editor<Word> Editor_T>
	inline NodePointer<Word> edit_node(const Editor_T &editor, NodePointer<Word> node_ptr, const NodeCoord<Word> &coord,
	                                   typename Editor_T::NodeState &state) {
		if (coord.level == m_config.GetNodeLevels() - 1)
			return edit_leaf<ThreadSafe>(editor, node_ptr, coord, state);

		std::array<Word, 9> unpacked_node = get_unpacked_node_array(node_ptr);
		Word &child_mask = unpacked_node[0];
		std::span<Word, 8> children = std::span<Word, 9>{unpacked_node}.template subspan<1>();
		std::array<typename Editor_T::NodeState, 8> child_states{};

		bool changed = false;

		for (Word i = 0; i < 8; ++i) {
			NodeCoord<Word> child_coord = coord.GetChildCoord(i);
			NodePointer<Word> child_ptr{children[i]};
			auto &child_state = child_states[i];
			NodePointer<Word> new_child_ptr = edit_switch(
			    editor, child_ptr, child_coord, child_state, state,
			    [&](NodePointer<Word> child_node_ptr) { return child_node_ptr; },
			    [&]() { return edit_node<ThreadSafe>(editor, child_ptr, child_coord, child_state); });

			changed |= new_child_ptr != child_ptr;
			children[i] = *new_child_ptr;
			child_mask ^= (Word{bool(new_child_ptr) != bool(child_ptr)} << i); // Flip if occurrence changed
		}

		editor.JoinNode(m_config, coord, state, child_states);

		return changed
		           ? (child_mask
		                  ? upsert_inner_node<ThreadSafe>(coord.level, get_packed_node_inplace(unpacked_node), node_ptr)
		                  : NodePointer<Word>::Null())
		           : node_ptr;
	}

public:
	inline virtual ~NodePoolBase() = default;
	inline explicit NodePoolBase(Config<Word> config) : m_config{std::move(config)} {
		static_assert(NodePool<Derived, Word>);
		m_bucket_level_bases = m_config.GetLevelBaseBucketIndices();
	}
	inline const auto &GetConfig() const { return m_config; }
	template <Editor<Word> Editor_T>
	inline auto Edit(NodePointer<Word> root_ptr, const Editor_T &editor,
	                 std::invocable<NodePointer<Word>, typename Editor_T::NodeState> auto &&on_edit_done) {
		make_filled_node_pointers();
		typename Editor_T::NodeState state{}, parent_state{};
		root_ptr = edit_switch(
		    editor, root_ptr, {}, state, parent_state, [&](NodePointer<Word> new_root_ptr) { return new_root_ptr; },
		    [&]() { return edit_node<false>(editor, root_ptr, {}, state); });
		return on_edit_done(root_ptr, std::move(state));
	}
	template <Editor<Word> Editor_T> inline NodePointer<Word> Edit(NodePointer<Word> root_ptr, const Editor_T &editor) {
		return Edit(root_ptr, editor, [&](NodePointer<Word> root_ptr, auto &&) { return root_ptr; });
	}
};

} // namespace hashdag

#endif // VKHASHDAG_HASHDAG_NODEPOOL_HPP

//
// Created by adamyuan on 1/31/24.
//

#pragma once
#ifndef VKHASHDAG_DAGCOLOROCTREE_HPP
#define VKHASHDAG_DAGCOLOROCTREE_HPP

#include <hashdag/VBRColor.hpp>
#include <hashdag/VBROctree.hpp>

#include "PagedVector.hpp"
#include "Range.hpp"
#include "VkPagedBuffer.hpp"

#include <array>
#include <parallel_hashmap/phmap.h>
#include <span>

class DAGColorPool final : public myvk::DeviceObjectBase {
public:
	template <typename T> using SafeLeafSpan = PagedSpan<SafePagedVector<uint32_t>, T>;
	struct Pointer {
		static constexpr uint32_t kDataBits = 30u;
		enum class Tag { kNode = 0, kColor, kLeaf, kNull };
		uint32_t pointer;
		inline Pointer() : Pointer(Tag::kNull, 0u) {}
		inline Pointer(Tag tag, uint32_t data) : pointer{(static_cast<uint32_t>(tag) << kDataBits) | data} {}
		inline Tag GetTag() const { return static_cast<Tag>(pointer >> kDataBits); }
		inline uint32_t GetData() const { return pointer & ((1u << kDataBits) - 1u); }

		inline bool operator==(const Pointer &r) const { return pointer == r.pointer; }
	};
	static_assert(sizeof(Pointer) == sizeof(uint32_t));

	struct Config {
		uint32_t leaf_level;
		uint32_t node_bits_per_node_page, word_bits_per_leaf_page;
		bool keep_history;
	};

private:
	using Node = std::array<Pointer, 8>;
	static_assert(sizeof(Node) == 8 * sizeof(uint32_t));

	static constexpr uint32_t kWordBitsPerNode = 3;
	static constexpr std::size_t kVBRStructWords = 2;
	static_assert(kVBRStructWords == sizeof(hashdag::VBRMacroBlock) / sizeof(uint32_t));
	static_assert(kVBRStructWords == sizeof(hashdag::VBRBlockHeader) / sizeof(uint32_t));

	SafePagedVector<Node> m_nodes;
	SafePagedVector<uint32_t> m_leaves;

	Config m_config{};
	Pointer m_root = {};

	// Vulkan Stuff
	myvk::Ptr<VkPagedBuffer> m_node_buffer, m_leaf_buffer;

	// Flush related Stuff
	uint32_t m_flushed_node_count{0}, m_flushed_node_page_count{0}, m_flushed_leaf_page_count{0};
	phmap::parallel_flat_hash_map<uint32_t, Range<uint32_t>, std::hash<uint32_t>, std::equal_to<>,
	                              std::allocator<std::pair<uint32_t, Range<uint32_t>>>, 6, std::mutex>
	    m_leaf_page_write_ranges;

	inline void write_leaf_chunk(std::size_t idx, const hashdag::VBRChunk<uint32_t, std::vector> &chunk) {
		// Write Sizes
		m_leaves.Write(idx++, [&](uint32_t &x) { x = chunk.GetMacroBlocks().size(); });
		m_leaves.Write(idx++, [&](uint32_t &x) { x = chunk.GetBlockHeaders().size(); });
		m_leaves.Write(idx++, [&](uint32_t &x) { x = chunk.GetWeightBits().GetWords().size(); });
		// Write MacroBlocks
		m_leaves.Write(idx, chunk.GetMacroBlocks().size() * kVBRStructWords,
		               [&](std::size_t offset, std::size_t, std::size_t, std::span<uint32_t> span) {
			               assert(idx % kVBRStructWords == 0 && offset % kVBRStructWords == 0);
			               std::ranges::copy(std::span{chunk.GetMacroBlocks().data() + offset / kVBRStructWords,
			                                           span.size() / kVBRStructWords},
			                                 (hashdag::VBRMacroBlock *)span.data());
		               });
		idx += chunk.GetMacroBlocks().size() * kVBRStructWords;
		// Write BlockHeaders
		m_leaves.Write(idx, chunk.GetBlockHeaders().size() * kVBRStructWords,
		               [&](std::size_t offset, std::size_t, std::size_t, std::span<uint32_t> span) {
			               assert(idx % kVBRStructWords == 0 && offset % kVBRStructWords == 0);
			               std::ranges::copy(std::span{chunk.GetBlockHeaders().data() + offset / kVBRStructWords,
			                                           span.size() / kVBRStructWords},
			                                 (hashdag::VBRBlockHeader *)span.data());
		               });
		idx += chunk.GetBlockHeaders().size() * kVBRStructWords;
		// Write WeightBits
		m_leaves.Write(idx, chunk.GetWeightBits().GetWords().size(),
		               [&](std::size_t offset, std::size_t, std::size_t, std::span<uint32_t> span) {
			               std::ranges::copy(std::span{chunk.GetWeightBits().GetWords().data() + offset, span.size()},
			                                 span.data());
		               });
	}

	inline hashdag::VBRChunk<uint32_t, SafeLeafSpan> fetch_leaf_chunk(std::size_t idx) const {
		// Read Sizes
		std::size_t macro_block_count = m_leaves.Read(idx++, std::identity{});
		std::size_t block_header_count = m_leaves.Read(idx++, std::identity{});
		std::size_t bit_word_count = m_leaves.Read(idx++, std::identity{});
		// Span for MacroBlocks
		SafeLeafSpan<hashdag::VBRMacroBlock> macro_block_span{m_leaves, idx, macro_block_count * kVBRStructWords};
		idx += macro_block_count * kVBRStructWords;
		// Span for BlockHeaders
		SafeLeafSpan<hashdag::VBRBlockHeader> block_header_span{m_leaves, idx, block_header_count * kVBRStructWords};
		idx += block_header_count * kVBRStructWords;
		// Span for WeightBits
		SafeLeafSpan<uint32_t> bit_span{m_leaves, idx, bit_word_count};
		return hashdag::VBRChunk<uint32_t, SafeLeafSpan>{macro_block_span, block_header_span,
		                                                 hashdag::VBRBitset<uint32_t, SafeLeafSpan>{bit_span}};
	}

	inline void mark_leaf(std::size_t idx, std::size_t count) {
		m_leaves.ForeachPage(idx, count,
		                     [this](std::size_t, uint32_t page_id, uint32_t page_offset, uint32_t inpage_count) {
			                     Range<uint32_t> range{.begin = page_offset, .end = page_offset + inpage_count};
			                     m_leaf_page_write_ranges.lazy_emplace_l(
			                         page_id, [&](auto &it) { it.second.Union(range); },
			                         [&](const auto &ctor) { ctor(page_id, range); });
		                     });
	}

public:
	inline DAGColorPool(const Config &config, myvk::Ptr<VkPagedBuffer> node_buffer,
	                    myvk::Ptr<VkPagedBuffer> leaf_buffer)
	    : m_config{config}, m_node_buffer{std::move(node_buffer)}, m_leaf_buffer{std::move(leaf_buffer)} {
		m_nodes.Reset(m_node_buffer->GetPageTotal(), config.node_bits_per_node_page);
		m_leaves.Reset(m_leaf_buffer->GetPageTotal(), config.word_bits_per_leaf_page);
	}
	static myvk::Ptr<DAGColorPool> Create(Config config, const std::vector<myvk::Ptr<myvk::Queue>> &queues);
	inline ~DAGColorPool() override = default;

	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const { return m_node_buffer->GetDevicePtr(); }

	inline const Config &GetConfig() const { return m_config; }

	inline Pointer GetChild(Pointer ptr, auto idx) const {
		return ptr.GetTag() == Pointer::Tag::kNode
		           ? m_nodes.Read(ptr.GetData(), [idx](Node node) -> Pointer { return node[idx]; })
		           : (ptr.GetTag() == Pointer::Tag::kColor ? ptr : Pointer{});
	}
	static inline hashdag::VBRColor GetFill(Pointer ptr) {
		return ptr.GetTag() == Pointer::Tag::kColor ? hashdag::VBRColor{ptr.GetData()} : hashdag::VBRColor{};
	}
	inline Pointer SetNode(Pointer ptr, std::span<const Pointer, 8> child_ptrs) {
		// Null
		if (std::ranges::all_of(child_ptrs, [](Pointer p) { return p.GetTag() == Pointer::Tag::kNull; }))
			return {};
		// Color
		if (Pointer c = child_ptrs[0]; c.GetTag() == Pointer::Tag::kColor &&
		                               std::ranges::all_of(child_ptrs.subspan(1), [c](Pointer p) { return p == c; }))
			return c;
		// Node (not changed)
		if (ptr.GetTag() == Pointer::Tag::kNode &&
		    std::ranges::equal(m_nodes.Read(ptr.GetData(), std::identity{}), child_ptrs))
			return ptr;
		// Need to create a new node
		auto opt_idx = m_nodes.Append([&](Node &node) { std::ranges::copy(child_ptrs, node.begin()); });
		return opt_idx ? Pointer{Pointer::Tag::kNode, (uint32_t)*opt_idx} : ptr;
	}
	inline static Pointer ClearNode(Pointer) { return Pointer{}; }
	inline static Pointer FillNode(Pointer, hashdag::VBRColor color) {
		return Pointer{Pointer::Tag::kColor, hashdag::RGB8Color{color.Get()}.GetData()};
	}

	inline hashdag::VBRChunk<uint32_t, SafeLeafSpan> GetLeaf(Pointer ptr) const {
		return ptr.GetTag() == Pointer::Tag::kLeaf ? fetch_leaf_chunk(ptr.GetData() + 1)
		                                           : hashdag::VBRChunk<uint32_t, SafeLeafSpan>{};
	}
	inline Pointer SetLeaf(Pointer ptr, hashdag::VBRChunk<uint32_t, std::vector> &&chunk) {
		std::size_t data_size = chunk.GetMacroBlocks().size() * kVBRStructWords +
		                        chunk.GetBlockHeaders().size() * kVBRStructWords +
		                        chunk.GetWeightBits().GetWords().size() + 4;

		static_assert(kVBRStructWords == 2);
		std::size_t append_size = (data_size & 1) ? data_size + 1 : data_size;

		if (!m_config.keep_history && ptr.GetTag() == Pointer::Tag::kLeaf) {
			std::size_t idx = ptr.GetData(), block_size = m_leaves.Read(idx, std::identity{});
			assert(idx % kVBRStructWords == 0 && block_size % kVBRStructWords == 0);
			if (data_size <= block_size) {
				// Space is enough, write and return
				mark_leaf(idx + 1, data_size - 1); // not mark the first "block size" indicator
				write_leaf_chunk(idx + 1, chunk);
				return ptr;
			}
			append_size = std::max(block_size << 1, append_size); // Double the space
		}

		assert(data_size <= append_size && append_size % kVBRStructWords == 0);

		// Append
		auto opt_idx = m_leaves.Append(append_size, [](auto &&...) {});
		// TODO: Test out-of-memory
		if (!opt_idx)
			return ptr;
		std::size_t idx = *opt_idx;
		mark_leaf(idx, data_size);
		m_leaves.Write(idx, [&](uint32_t &x) { x = append_size; });
		write_leaf_chunk(idx + 1, chunk);
		return Pointer{Pointer::Tag::kLeaf, (uint32_t)idx};
	}
	inline uint32_t GetLeafLevel() const { return m_config.leaf_level; }

	void Flush(const myvk::Ptr<VkSparseBinder> &binder);

	inline Pointer GetRoot() const { return m_root; }
	inline void SetRoot(Pointer root) { m_root = root; }

	inline const auto &GetNodeBuffer() const { return m_node_buffer; }
	inline const auto &GetLeafBuffer() const { return m_leaf_buffer; }
};

static_assert(hashdag::VBROctree<DAGColorPool, uint32_t>);

#endif // VKHASHDAG_DAGCOLOROCTREE_HPP

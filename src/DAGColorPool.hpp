//
// Created by adamyuan on 1/31/24.
//

#pragma once
#ifndef VKHASHDAG_DAGCOLOROCTREE_HPP
#define VKHASHDAG_DAGCOLOROCTREE_HPP

#include <hashdag/VBRColor.hpp>
#include <hashdag/VBROctree.hpp>

#include "PagedVector.hpp"
#include "VkPagedBuffer.hpp"

#include <array>
#include <span>

class DAGColorPool {
public:
	template <typename T> using SafeLeafSpan = PagedSpan<SafePagedVector<uint32_t>, T>;
	using Writer = hashdag::VBRChunkWriter<uint32_t, SafeLeafSpan>;
	struct Pointer {
		static constexpr uint32_t kDataBits = 30u;
		enum class Tag { kNull = 0, kNode, kLeaf, kColor };
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

	Config m_config;
	Pointer m_root = {};

	// Vulkan Stuff
	myvk::Ptr<myvk::Device> m_device_ptr;
	myvk::Ptr<myvk::Queue> m_main_queue_ptr, m_sparse_queue_ptr;
	myvk::Ptr<VkPagedBuffer> m_node_buffer, m_leaf_buffer;
	void create_vk_buffer();

	// Flush related Stuff
	uint32_t m_flushed_nodes, m_flushed_leaves;

public:
	inline explicit DAGColorPool(const myvk::Ptr<myvk::Queue> &main_queue_ptr,
	                             const myvk::Ptr<myvk::Queue> &sparse_queue_ptr, const Config &config)
	    : m_device_ptr{main_queue_ptr->GetDevicePtr()}, m_main_queue_ptr{main_queue_ptr},
	      m_sparse_queue_ptr{sparse_queue_ptr}, m_config{config} {
		create_vk_buffer();
		m_nodes.Reset(m_node_buffer->GetPageCount(), m_config.node_bits_per_node_page);
	}

	inline Pointer GetNode(Pointer ptr, auto idx) const {
		return ptr.GetTag() == Pointer::Tag::kNode
		           ? m_nodes.Read(ptr.GetData(), [idx](Node node) -> Pointer { return node[idx]; })
		           : Pointer{};
	}
	inline Pointer SetNode(Pointer ptr, std::span<const Pointer, 8> child_ptrs) {
		if (ptr.GetTag() != Pointer::Tag::kNode ||
		    !std::ranges::equal(m_nodes.Read(ptr.GetData(), [](Node node) { return node; }), child_ptrs)) {
			auto opt_idx = m_nodes.Append([&](Node &node) { std::ranges::copy(child_ptrs, node.begin()); });
			ptr = opt_idx ? Pointer{Pointer::Tag::kNode, (uint32_t)*opt_idx} : ptr;
		}
		return ptr;
	}
	inline static Pointer ClearNode(Pointer) { return Pointer{}; }
	inline static Pointer FillNode(Pointer, hashdag::VBRColor color) {
		return Pointer{Pointer::Tag::kColor, hashdag::RGB8Color{color.Get()}.GetData()};
	}

	inline Writer WriteLeaf(Pointer ptr) const {
		if (ptr.GetTag() != Pointer::Tag::kLeaf)
			return Writer{};

		std::size_t idx = ptr.GetData();
		// Read Sizes
		std::size_t macro_block_count = m_leaves.Read(idx + 1, std::identity{});
		std::size_t block_header_count = m_leaves.Read(idx + 2, std::identity{});
		std::size_t bit_word_count = m_leaves.Read(idx + 3, std::identity{});
		idx += 4;
		// Span for MacroBlocks
		SafeLeafSpan<hashdag::VBRMacroBlock> macro_block_span{m_leaves, idx, macro_block_count * kVBRStructWords};
		idx += macro_block_count * kVBRStructWords;
		// Span for BlockHeaders
		SafeLeafSpan<hashdag::VBRBlockHeader> block_header_span{m_leaves, idx, block_header_count * kVBRStructWords};
		idx += block_header_count * kVBRStructWords;
		// Span for WeightBits
		SafeLeafSpan<uint32_t> bit_span{m_leaves, idx, bit_word_count};
		return Writer{hashdag::VBRChunk<uint32_t, SafeLeafSpan>{macro_block_span, block_header_span,
		                                                        hashdag::VBRBitset<uint32_t, SafeLeafSpan>{bit_span}}};
	}
	inline Pointer FlushLeaf(Pointer ptr, Writer &&writer) {
		hashdag::VBRChunk<uint32_t, std::vector> chunk = writer.Flush();
		std::size_t data_size = chunk.GetMacroBlocks().size() * kVBRStructWords +
		                        chunk.GetBlockHeaders().size() * kVBRStructWords +
		                        chunk.GetWeightBits().GetWords().size() + 4;

		const auto write_chunk = [this, &chunk](std::size_t idx) {
			// Write Sizes
			m_leaves.Write(idx + 1, [&](uint32_t &x) { x = chunk.GetMacroBlocks().size(); });
			m_leaves.Write(idx + 2, [&](uint32_t &x) { x = chunk.GetBlockHeaders().size(); });
			m_leaves.Write(idx + 3, [&](uint32_t &x) { x = chunk.GetWeightBits().GetWords().size(); });
			idx += 4;
			// Write MacroBlocks
			m_leaves.Write(idx, chunk.GetMacroBlocks().size() * kVBRStructWords,
			               [&](std::size_t offset, std::span<uint32_t> span) {
				               assert(idx % kVBRStructWords == 0 && offset % kVBRStructWords == 0);
				               std::ranges::copy(std::span{chunk.GetMacroBlocks().data() + offset / kVBRStructWords,
				                                           span.size() / kVBRStructWords},
				                                 (hashdag::VBRMacroBlock *)span.data());
			               });
			idx += chunk.GetMacroBlocks().size() * kVBRStructWords;
			// Write BlockHeaders
			m_leaves.Write(idx, chunk.GetBlockHeaders().size() * kVBRStructWords,
			               [&](std::size_t offset, std::span<uint32_t> span) {
				               assert(idx % kVBRStructWords == 0 && offset % kVBRStructWords == 0);
				               std::ranges::copy(std::span{chunk.GetBlockHeaders().data() + offset / kVBRStructWords,
				                                           span.size() / kVBRStructWords},
				                                 (hashdag::VBRBlockHeader *)span.data());
			               });
			idx += chunk.GetBlockHeaders().size() * kVBRStructWords;
			// Write WeightBits
			m_leaves.Write(
			    idx, chunk.GetWeightBits().GetWords().size(), [&](std::size_t offset, std::span<uint32_t> span) {
				    std::ranges::copy(std::span{chunk.GetWeightBits().GetWords().data() + offset, span.size()},
				                      span.data());
			    });
		};

		static_assert(kVBRStructWords == 2);
		std::size_t append_size = (data_size & 1) ? data_size + 1 : data_size;

		if (!m_config.keep_history && ptr.GetTag() == Pointer::Tag::kLeaf) {
			std::size_t idx = ptr.GetData(), block_size = m_leaves.Read(idx, std::identity{});
			assert(idx % kVBRStructWords == 0 && block_size % kVBRStructWords == 0);
			if (data_size <= block_size) {
				// Space is enough, write and return
				write_chunk(idx);
				return ptr;
			} else
				append_size = std::max(block_size << 1, append_size); // Double the space
		}

		assert(data_size <= append_size && append_size % kVBRStructWords == 0);

		// Append
		auto opt_idx = m_leaves.Append(append_size, [](auto &&, auto &&) {});
		if (!opt_idx)
			return ptr;
		std::size_t idx = *opt_idx;
		m_leaves.Write(idx, [&](uint32_t &x) { x = append_size; });
		write_chunk(idx);
		return Pointer{Pointer::Tag::kLeaf, (uint32_t)idx};
	}
	inline uint32_t GetLeafLevel() const { return m_config.leaf_level; }

	inline Pointer GetRoot() const { return m_root; }
	inline void SetRoot(Pointer root) { m_root = root; }

	inline const Config &GetConfig() const { return m_config; }
};

static_assert(hashdag::VBROctree<DAGColorPool, uint32_t>);

#endif // VKHASHDAG_DAGCOLOROCTREE_HPP

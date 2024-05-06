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
		uint32_t level_count, leaf_level;
		uint32_t node_bits_per_node_page, word_bits_per_leaf_page;
	};

private:
	using Node = std::array<Pointer, 8>;
	static_assert(sizeof(Node) == 8 * sizeof(uint32_t));
	static constexpr uint32_t kWordBitsPerNode = 3;

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
		return ptr.GetTag() == Pointer::Tag::kLeaf
		           ? m_leaves.Read(ptr.GetData(), [](const Leaf &leaf) { return Writer{leaf.chunk}; })
		           : Writer{};
	}
	inline Pointer FlushLeaf(Pointer ptr, Writer &&writer) {
		if (ptr.GetTag() != Pointer::Tag::kLeaf)
			ptr =
			    Pointer{Pointer::Tag::kLeaf, (uint32_t)m_leaves.Append(Leaf{.chunk = writer.Flush()}, [](auto &&) {})};
		else
			m_leaves.Write(ptr.GetData(), [&](Leaf &leaf) { leaf.chunk = writer.Flush(); });
		return ptr;
	}
	inline uint32_t GetLeafLevel() const { return m_config.leaf_level; }

	inline Pointer GetRoot() const { return m_root; }
	inline void SetRoot(Pointer root) { m_root = root; }
};

static_assert(hashdag::VBROctree<DAGColorPool, uint32_t>);

#endif // VKHASHDAG_DAGCOLOROCTREE_HPP

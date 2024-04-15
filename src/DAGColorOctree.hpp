//
// Created by adamyuan on 1/31/24.
//

#pragma once
#ifndef VKHASHDAG_DAGCOLOROCTREE_HPP
#define VKHASHDAG_DAGCOLOROCTREE_HPP

#include <hashdag/VBRColor.hpp>
#include <hashdag/VBROctree.hpp>

#include <array>
#include <ranges>
#include <shared_mutex>
#include <span>

class DAGColorOctree {
private:
	template <typename T> using ConstSpan = std::span<const T>;

public:
	using Writer = hashdag::VBRChunkWriter<uint32_t, ConstSpan>;
	struct Pointer {
		enum class Tag { kNode = 0, kLeaf, kColor, kNull };
		uint32_t pointer;
		inline Pointer() : Pointer(Tag::kNull, 0u) {}
		inline Pointer(Tag tag, uint32_t data) : pointer{(static_cast<uint32_t>(tag) << 30u) | data} {}
		inline Tag GetTag() const { return static_cast<Tag>(pointer >> 30u); }
		inline uint32_t GetData() const { return pointer & 0x3fffffffu; }

		inline bool operator==(const Pointer &r) const { return pointer == r.pointer; }
	};

private:
	using Node = std::array<Pointer, 8>;
	struct Leaf {
		hashdag::VBRChunk<uint32_t, std::vector> chunk;
	};
	std::vector<Node> m_nodes;
	std::vector<Leaf> m_leaves;
	std::shared_mutex m_mutex;

	uint32_t m_leaf_level{};

public:
	// TODO: Make it thread-safe
	inline Pointer GetNode(Pointer ptr, auto idx) const {
		return ptr.GetTag() == Pointer::Tag::kNode ? m_nodes[ptr.GetData()][idx] : Pointer{};
	}
	inline Pointer SetNode(Pointer ptr, std::span<const Pointer, 8> child_ptrs) {
		if (ptr.GetTag() != Pointer::Tag::kNode || !std::ranges::equal(m_nodes[ptr.GetData()], child_ptrs)) {
			ptr = Pointer{Pointer::Tag::kNode, (uint32_t)m_nodes.size()};
			m_nodes.emplace_back();
			std::ranges::copy(child_ptrs, m_nodes.back().begin());
		}
		return ptr;
	}
	inline static Pointer ClearNode(Pointer) { return Pointer{}; }
	inline static Pointer FillNode(Pointer, hashdag::VBRColor color) {
		return Pointer{Pointer::Tag::kColor, hashdag::RGB8Color{color.Get()}.GetData()};
	}

	inline Writer WriteLeaf(Pointer ptr) const {
		return ptr.GetTag() == Pointer::Tag::kLeaf ? Writer{m_leaves[ptr.GetData()].chunk} : Writer{};
	}
	inline Pointer FlushLeaf(Pointer ptr, Writer &&writer) {
		if (ptr.GetTag() != Pointer::Tag::kLeaf) {
			ptr = Pointer{Pointer::Tag::kLeaf, (uint32_t)m_leaves.size()};
			m_leaves.emplace_back(writer.Flush());
		} else
			m_leaves[ptr.GetData()].chunk = writer.Flush();
		return ptr;
	}
	inline uint32_t GetLeafLevel() const { return m_leaf_level; }
};

static_assert(hashdag::VBROctree<DAGColorOctree, uint32_t>);

#endif // VKHASHDAG_DAGCOLOROCTREE_HPP

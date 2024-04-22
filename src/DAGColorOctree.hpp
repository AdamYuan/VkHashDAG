//
// Created by adamyuan on 1/31/24.
//

#pragma once
#ifndef VKHASHDAG_DAGCOLOROCTREE_HPP
#define VKHASHDAG_DAGCOLOROCTREE_HPP

#include <hashdag/VBRColor.hpp>
#include <hashdag/VBROctree.hpp>

#include <array>
#include <mutex>
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

	template <typename T> struct SafeVector {
		std::vector<T> vector;
		mutable std::shared_mutex mutex;

		inline auto Visit(std::size_t idx, std::invocable<const T &> auto &&visitor) const {
			std::shared_lock lock{mutex};
			return visitor(vector[idx]);
		}
		inline auto Visit(std::size_t idx, std::invocable<T &> auto &&visitor) {
			std::shared_lock lock{mutex};
			return visitor(vector[idx]);
		}
		inline std::size_t Append(T &&t, std::invocable<T &> auto &&appender) {
			std::unique_lock lock{mutex};
			std::size_t idx = vector.size();
			vector.push_back(std::move(t));
			appender(vector.back());
			return idx;
		}
	};
	SafeVector<Node> m_nodes;
	SafeVector<Leaf> m_leaves;

	uint32_t m_leaf_level{};

public:
	inline Pointer GetNode(Pointer ptr, auto idx) const {
		return ptr.GetTag() == Pointer::Tag::kNode
		           ? m_nodes.Visit(ptr.GetData(), [idx](Node node) -> Pointer { return node[idx]; })
		           : Pointer{};
	}
	inline Pointer SetNode(Pointer ptr, std::span<const Pointer, 8> child_ptrs) {
		if (ptr.GetTag() != Pointer::Tag::kNode ||
		    !std::ranges::equal(m_nodes.Visit(ptr.GetData(), [](Node node) { return node; }), child_ptrs)) {
			ptr =
			    Pointer{Pointer::Tag::kNode,
			            (uint32_t)m_nodes.Append({}, [&](Node &node) { std::ranges::copy(child_ptrs, node.begin()); })};
		}
		return ptr;
	}
	inline static Pointer ClearNode(Pointer) { return Pointer{}; }
	inline static Pointer FillNode(Pointer, hashdag::VBRColor color) {
		return Pointer{Pointer::Tag::kColor, hashdag::RGB8Color{color.Get()}.GetData()};
	}

	inline Writer WriteLeaf(Pointer ptr) const {
		return ptr.GetTag() == Pointer::Tag::kLeaf
		           ? m_leaves.Visit(ptr.GetData(), [](const Leaf &leaf) { return Writer{leaf.chunk}; })
		           : Writer{};
	}
	inline Pointer FlushLeaf(Pointer ptr, Writer &&writer) {
		if (ptr.GetTag() != Pointer::Tag::kLeaf)
			ptr =
			    Pointer{Pointer::Tag::kLeaf, (uint32_t)m_leaves.Append(Leaf{.chunk = writer.Flush()}, [](auto &&) {})};
		else
			m_leaves.Visit(ptr.GetData(), [&](Leaf &leaf) { leaf.chunk = writer.Flush(); });
		return ptr;
	}
	inline uint32_t GetLeafLevel() const { return m_leaf_level; }
};

static_assert(hashdag::VBROctree<DAGColorOctree, uint32_t>);

#endif // VKHASHDAG_DAGCOLOROCTREE_HPP

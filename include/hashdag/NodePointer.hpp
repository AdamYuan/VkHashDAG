//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_NODEPOINTER_HPP
#define VKHASHDAG_NODEPOINTER_HPP

#include <concepts>

namespace hashdag {

template <std::unsigned_integral Word> class NodePointer {
private:
	Word m_node;

public:
	inline constexpr NodePointer() : m_node(-1) {}
	inline constexpr NodePointer(Word node) : m_node{node} {}

	inline constexpr bool HasValue() const { return m_node != -1; }
	inline constexpr operator bool() const { return HasValue(); }

	inline constexpr bool operator==(NodePointer r) const { return m_node == r.m_node; }
	inline constexpr bool operator!=(NodePointer r) const { return m_node != r.m_node; }

	inline constexpr Word Value() const { return m_node; }
	inline constexpr Word operator*() const { return Value(); }

	inline constexpr static NodePointer Null() { return NodePointer{}; }
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOINTER_HPP

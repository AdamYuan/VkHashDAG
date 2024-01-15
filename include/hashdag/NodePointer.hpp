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
	inline NodePointer() : m_node(-1) {}
	inline NodePointer(Word node) : m_node{node} {}

	inline bool HasValue() const { return m_node != -1; }
	inline operator bool() const { return HasValue(); }

	inline bool operator==(NodePointer r) const { return m_node == r.m_node; }
	inline bool operator!=(NodePointer r) const { return m_node != r.m_node; }

	inline Word Value() const { return m_node; }
	inline Word operator*() const { return Value(); }

	inline static NodePointer Null() { return {}; }
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOINTER_HPP

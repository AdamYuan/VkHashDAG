//
// Created by adamyuan on 1/31/24.
//

#pragma once
#ifndef VKHASHDAG_DAGCOLORPOOL_HPP
#define VKHASHDAG_DAGCOLORPOOL_HPP

#include <hashdag/VBRColor.hpp>

#include <array>
#include <shared_mutex>

class DAGColorPool {
private:
	using OctreeNode = std::array<uint32_t, 8>;
	static_assert(sizeof(OctreeNode) == 8 * sizeof(uint32_t));

	std::vector<hashdag::VBRColorBlock> m_color_blocks;
	std::vector<OctreeNode> m_octree;
	std::shared_mutex m_mutex;

public:
	const hashdag::VBRColorBlock *ReadBlock(const hashdag::NodeCoord<uint32_t> &coord) const;
	hashdag::VBRColorBlockWriter WriteBlock(const hashdag::NodeCoord<uint32_t> &coord);
	void FreeBlock(const hashdag::NodeCoord<uint32_t> &coord);
};

#endif // VKHASHDAG_DAGCOLORPOOL_HPP

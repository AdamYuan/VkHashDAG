//
// Created by adamyuan on 1/31/24.
//

#pragma once
#ifndef VKHASHDAG_DAGCOLOROCTREE_HPP
#define VKHASHDAG_DAGCOLOROCTREE_HPP

#include <hashdag/VBRColor.hpp>

#include <array>
#include <shared_mutex>
#include <span>

class DAGColorOctree {
private:
	template <typename T> using ConstSpan = std::span<const T>;

public:
	using ChunkWriter = hashdag::VBRChunkWriter<uint32_t, ConstSpan>;
	struct Chunk {
		hashdag::VBRChunk<uint32_t, std::vector> chunk;
	};
	struct Pointer {
		uint32_t pointer;


	};

private:
	using OctreeNode = std::array<uint32_t, 8>;
	static_assert(sizeof(OctreeNode) == 8 * sizeof(uint32_t));

	std::vector<std::vector<uint8_t>> m_chunks;
	std::vector<OctreeNode> m_octree;
	std::shared_mutex m_mutex;

public:
	const hashdag::VBRChunk *ReadBlock(const hashdag::NodeCoord<uint32_t> &coord) const;
	hashdag::VBRChunkWriter WriteBlock(const hashdag::NodeCoord<uint32_t> &coord);
	void FreeBlock(const hashdag::NodeCoord<uint32_t> &coord);
};

#endif // VKHASHDAG_DAGCOLOROCTREE_HPP

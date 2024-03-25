//
// Created by adamyuan on 1/31/24.
//

#include "DAGColorPool.hpp"

const hashdag::VBRColorBlock *DAGColorPool::ReadBlock(const hashdag::NodeCoord<uint32_t> &coord) const {
	return nullptr;
}
hashdag::VBRColorBlockWriter DAGColorPool::WriteBlock(const hashdag::NodeCoord<uint32_t> &coord) {
	return hashdag::VBRColorBlockWriter(nullptr, 0);
}
void DAGColorPool::FreeBlock(const hashdag::NodeCoord<uint32_t> &coord) {

}

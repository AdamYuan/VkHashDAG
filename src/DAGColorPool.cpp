//
// Created by adamyuan on 1/31/24.
//

#include "DAGColorPool.hpp"

#include <cassert>

void DAGColorPool::create_vk_buffer() {
	m_node_buffer = VkPagedBuffer::Create(
	    m_device_ptr,
	    std::min(m_device_ptr->GetPhysicalDevicePtr()->GetProperties().vk10.limits.sparseAddressSpaceSize,
	             VkDeviceSize(1u << Pointer::kDataBits) * sizeof(Node)),
	    [this](const VkMemoryRequirements &mem_req) {
		    assert(mem_req.alignment > 0 && std::popcount(mem_req.alignment) == 1);
		    uint32_t node_bits_per_alignment =
		        std::bit_width(std::max(mem_req.alignment / sizeof(Node), (VkDeviceSize)1)) - 1u;
		    m_config.node_bits_per_node_page = std::max(m_config.node_bits_per_node_page, node_bits_per_alignment);
		    return (1u << m_config.node_bits_per_node_page) * sizeof(Node);
	    },
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, {m_main_queue_ptr, m_sparse_queue_ptr});

	m_leaf_buffer = VkPagedBuffer::Create(
	    m_device_ptr,
	    std::min(m_device_ptr->GetPhysicalDevicePtr()->GetProperties().vk10.limits.sparseAddressSpaceSize,
	             VkDeviceSize(1u << Pointer::kDataBits) * sizeof(uint32_t)),
	    [this](const VkMemoryRequirements &mem_req) {
		    assert(mem_req.alignment > 0 && std::popcount(mem_req.alignment) == 1);
		    uint32_t word_bits_per_alignment =
		        std::bit_width(std::max(mem_req.alignment / sizeof(uint32_t), (VkDeviceSize)1)) - 1u;
		    m_config.word_bits_per_leaf_page = std::max(m_config.word_bits_per_leaf_page, word_bits_per_alignment);
		    return (1u << m_config.word_bits_per_leaf_page) * sizeof(uint32_t);
	    },
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, {m_main_queue_ptr, m_sparse_queue_ptr});
}
//
// Created by adamyuan on 1/31/24.
//

#include "DAGColorPool.hpp"

#include <cassert>

void DAGColorPool::create_vk_buffer(std::vector<myvk::Ptr<myvk::Queue>> &&queues) {
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
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, queues);

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
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, queues);
}

void DAGColorPool::Flush(const myvk::Ptr<VkSparseBinder> &binder) {
	const auto update_pages = [&binder]<typename T>(const myvk::Ptr<VkPagedBuffer> &buffer,
	                                                const SafePagedVector<T> &vector, uint32_t *p_flushed_page_count) {
		uint32_t page_count = vector.GetPageCount(), flushed_page_count = *p_flushed_page_count;
		if (flushed_page_count < page_count)
			buffer->Alloc(binder, std::views::iota(flushed_page_count, page_count));
		else if (page_count < flushed_page_count)
			buffer->Free(binder, std::views::iota(page_count, flushed_page_count));
		*p_flushed_page_count = page_count;
	};
	update_pages(m_node_buffer, m_nodes, &m_flushed_node_page_count);
	update_pages(m_leaf_buffer, m_leaves, &m_flushed_leaf_page_count);

	{ // Flush Nodes
		uint32_t node_count = m_nodes.GetCount();
		uint32_t idx = m_flushed_node_count < node_count ? m_flushed_node_count : 0, count = node_count - idx;
		m_nodes.Read(idx, count,
		             [this](std::size_t, std::size_t page_id, std::size_t page_offset, std::span<const Node> span) {
			             std::ranges::copy(span, m_node_buffer->GetMappedPage<Node>(page_id) + page_offset);
		             });
		m_flushed_node_count = node_count;
	}

	{ // Flush Leaves
		for (const auto &[page_id, range] : m_leaf_page_write_ranges) {
			uint32_t *p_page = m_leaves.GetPage(page_id);
			std::copy(p_page + range.begin, p_page + range.end,
			          m_leaf_buffer->GetMappedPage<uint32_t>(page_id) + range.begin);
		}
		m_leaf_page_write_ranges.clear();
	}
}
//
// Created by adamyuan on 4/30/24.
//

#include "VkPagedBuffer.hpp"

VkPagedBuffer::~VkPagedBuffer() {
	std::vector<VmaAllocation> allocations;
	for (const auto &page : m_pages)
		if (page.allocation != VK_NULL_HANDLE)
			allocations.push_back(page.allocation);
	if (!allocations.empty())
		vmaFreeMemoryPages(m_device_ptr->GetAllocatorHandle(), allocations.size(), allocations.data());

	if (m_buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(m_device_ptr->GetHandle(), m_buffer, nullptr);
}
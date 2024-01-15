//
// Created by adamyuan on 1/15/24.
//

#include "NodePool.hpp"

#include <unordered_set>

void NodePool::create_buffer() {
	m_gpu_buffer_size = (VkDeviceSize)GetConfig().GetTotalWords() * sizeof(uint32_t);

	VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	create_info.size = m_gpu_buffer_size;
	create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	create_info.flags = VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT | VK_BUFFER_CREATE_SPARSE_BINDING_BIT;

	std::array<uint32_t, 2> queue_families = {m_main_queue_ptr->GetFamilyIndex(), m_sparse_queue_ptr->GetFamilyIndex()};
	if (queue_families[0] == queue_families[1]) {
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	} else {
		create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
		create_info.queueFamilyIndexCount = queue_families.size();
		create_info.pQueueFamilyIndices = queue_families.data();
	}

	vkCreateBuffer(m_device_ptr->GetHandle(), &create_info, nullptr, &m_gpu_buffer);
}
void NodePool::destroy_buffer() {
	std::vector<VmaAllocation> allocations;
	for (const auto &gpu_page : m_gpu_pages)
		if (gpu_page.allocation != VK_NULL_HANDLE)
			allocations.push_back(gpu_page.allocation);
	if (!allocations.empty())
		vmaFreeMemoryPages(m_device_ptr->GetAllocatorHandle(), allocations.size(), allocations.data());

	vkDestroyBuffer(m_device_ptr->GetHandle(), m_gpu_buffer, nullptr);
}

void NodePool::Flush(const myvk::SemaphoreGroup &wait_semaphores, const myvk::SemaphoreGroup &signal_semaphores,
                     const myvk::Ptr<myvk::Fence> &fence) {
	std::unordered_set<uint32_t> missing_gpu_page_indices;
	for (const auto &it : m_gpu_write_ranges) {
		uint32_t page_id = it.first, gpu_page_id = page_id >> m_page_bits_per_gpu_page;
		if (m_gpu_pages[gpu_page_id].allocation == VK_NULL_HANDLE)
			missing_gpu_page_indices.insert(gpu_page_id);
	}

	if (!missing_gpu_page_indices.empty()) {
		std::vector<VmaAllocation> allocations(missing_gpu_page_indices.size());
		std::vector<VmaAllocationInfo> allocation_infos(missing_gpu_page_indices.size());

		VmaAllocationCreateInfo create_info = {};
		create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		// create_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		vmaAllocateMemoryPages(m_device_ptr->GetAllocatorHandle(), &m_gpu_page_memory_requirements, &create_info,
		                       missing_gpu_page_indices.size(), allocations.data(), allocation_infos.data());

		VkBindSparseInfo bind_sparse_info = {VK_STRUCTURE_TYPE_BIND_SPARSE_INFO};
		bind_sparse_info.waitSemaphoreCount = wait_semaphores.GetCount();
		bind_sparse_info.pWaitSemaphores = wait_semaphores.GetSemaphoresPtr();
		bind_sparse_info.signalSemaphoreCount = signal_semaphores.GetCount();
		bind_sparse_info.pSignalSemaphores = signal_semaphores.GetSemaphoresPtr();

		std::vector<VkSparseMemoryBind> sparse_memory_binds(missing_gpu_page_indices.size());
		VkSparseBufferMemoryBindInfo sparse_buffer_memory_bind_info = {
		    .buffer = m_gpu_buffer,
		    .bindCount = (uint32_t)sparse_memory_binds.size(),
		    .pBinds = sparse_memory_binds.data(),
		};

		bind_sparse_info.bufferBindCount = 1;
		bind_sparse_info.pBufferBinds = &sparse_buffer_memory_bind_info;

		uint32_t counter = 0;
		for (uint32_t gpu_page_id : missing_gpu_page_indices) {
			const auto &allocation_info = allocation_infos[counter];

			m_gpu_pages[gpu_page_id] = {
			    .allocation = allocations[counter],
			    .info = allocation_info,
			};

			VkDeviceSize resource_offset = gpu_page_id * m_gpu_page_memory_requirements.size;
			sparse_memory_binds[counter] = {
			    .resourceOffset = resource_offset,
			    .size = std::min(allocation_info.size, m_gpu_buffer_size - resource_offset),
			    .memory = allocation_info.deviceMemory,
			    .memoryOffset = allocation_info.offset,
			    .flags = 0,
			};

			++counter;
		}

		vkQueueBindSparse(m_sparse_queue_ptr->GetHandle(), 1, &bind_sparse_info,
		                  fence ? fence->GetHandle() : VK_NULL_HANDLE);
	}

	for (const auto &it : m_gpu_write_ranges) {
		uint32_t page_id = it.first, gpu_page_id = page_id >> m_page_bits_per_gpu_page;
		const Range &range = it.second;
		uint32_t gpu_page_offset =
		    ((page_id & ((1u << m_page_bits_per_gpu_page) - 1)) << GetConfig().word_bits_per_page) | range.begin;

		const auto &gpu_page = m_gpu_pages[gpu_page_id];

		auto p_mapped = (uint32_t *)gpu_page.info.pMappedData;
		std::copy(m_pages[page_id].get() + range.begin, m_pages[page_id].get() + range.end, p_mapped + gpu_page_offset);
	}
}

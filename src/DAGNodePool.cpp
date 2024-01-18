//
// Created by adamyuan on 1/15/24.
//

#include "DAGNodePool.hpp"

#include <unordered_set>

void DAGNodePool::create_buffer() {
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
void DAGNodePool::create_gpu_pages() {
	vkGetBufferMemoryRequirements(m_device_ptr->GetHandle(), m_gpu_buffer, &m_gpu_page_memory_requirements);
	m_page_bits_per_gpu_page =
	    m_gpu_page_memory_requirements.alignment <= GetConfig().GetWordsPerPage() * sizeof(uint32_t)
	        ? 0
	        : std::countr_zero(m_gpu_page_memory_requirements.alignment / sizeof(uint32_t)) -
	              GetConfig().word_bits_per_page;
	m_gpu_page_memory_requirements.size =
	    (1u << (GetConfig().word_bits_per_page + m_page_bits_per_gpu_page)) * sizeof(uint32_t);

	m_gpu_pages.resize(1 + (GetConfig().GetTotalPages() >> m_page_bits_per_gpu_page));
}
void DAGNodePool::destroy_buffer() {
	std::vector<VmaAllocation> allocations;
	for (const auto &gpu_page : m_gpu_pages)
		if (gpu_page.allocation != VK_NULL_HANDLE)
			allocations.push_back(gpu_page.allocation);
	if (!allocations.empty())
		vmaFreeMemoryPages(m_device_ptr->GetAllocatorHandle(), allocations.size(), allocations.data());

	vkDestroyBuffer(m_device_ptr->GetHandle(), m_gpu_buffer, nullptr);
}

void DAGNodePool::create_descriptor() {
	VkDescriptorSetLayoutBinding layout_binding = {};
	layout_binding.binding = 0;
	layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	layout_binding.descriptorCount = 1;
	layout_binding.stageFlags = VK_SHADER_STAGE_ALL;

	auto descriptor_set_layout = myvk::DescriptorSetLayout::Create(m_device_ptr, {layout_binding});
	auto descriptor_pool = myvk::DescriptorPool::Create(m_device_ptr, 1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}});
	m_descriptor_set = myvk::DescriptorSet::Create(descriptor_pool, descriptor_set_layout);

	// Write descriptor
	VkDescriptorBufferInfo buffer_info = {
	    .buffer = m_gpu_buffer,
	    .offset = 0,
	    .range = m_gpu_buffer_size,
	};
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_descriptor_set->GetHandle();
	write.dstBinding = 0;
	write.dstArrayElement = 0;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.descriptorCount = 1;
	write.pBufferInfo = &buffer_info;

	vkUpdateDescriptorSets(m_device_ptr->GetHandle(), 1, &write, 0, nullptr);
}

bool DAGNodePool::FlushMissingPages(const myvk::SemaphoreGroup &wait_semaphores,
                                    const myvk::SemaphoreGroup &signal_semaphores,
                                    const myvk::Ptr<myvk::Fence> &fence) {
	auto gpu_missing_write_ranges = m_gpu_missing_write_ranges.lock_table();
	if (gpu_missing_write_ranges.empty())
		return false;

	std::unordered_set<uint32_t> missing_gpu_page_indices;
	for (const auto &it : gpu_missing_write_ranges) {
		uint32_t page_id = it.first, gpu_page_id = page_id >> m_page_bits_per_gpu_page;
		if (m_gpu_pages[gpu_page_id].allocation == VK_NULL_HANDLE)
			missing_gpu_page_indices.insert(gpu_page_id);
	}

	std::vector<VmaAllocation> allocations(missing_gpu_page_indices.size());
	std::vector<VmaAllocationInfo> allocation_infos(missing_gpu_page_indices.size());

	VmaAllocationCreateInfo create_info = {};
	create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
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
		    .p_mapped_data = (uint32_t *)allocation_info.pMappedData,
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

	for (const auto &it : gpu_missing_write_ranges) {
		uint32_t page_id = it.first, gpu_page_id = page_id >> m_page_bits_per_gpu_page;
		const Range &range = it.second;

		const auto &gpu_page = m_gpu_pages[gpu_page_id];
		uint32_t gpu_page_offset =
		    ((page_id & ((1u << m_page_bits_per_gpu_page) - 1)) << GetConfig().word_bits_per_page) | range.begin;
		std::copy(m_pages[page_id].get() + range.begin, m_pages[page_id].get() + range.end,
		          gpu_page.p_mapped_data + gpu_page_offset);
	}
	gpu_missing_write_ranges.clear();

	return true;
}

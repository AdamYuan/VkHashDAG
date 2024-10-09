//
// Created by adamyuan on 4/30/24.
//

#pragma once
#ifndef VKPAGEDBUFFER_HPP
#define VKPAGEDBUFFER_HPP

#include <cassert>
#include <concepts>
#include <ranges>
#include <set>
#include <span>

#include <myvk/BufferBase.hpp>
#include <myvk/Fence.hpp>
#include <myvk/Queue.hpp>
#include <myvk/Semaphore.hpp>

#include "VkSparseBinder.hpp"

class VkPagedBuffer final : public myvk::BufferBase {
private:
	struct Page {
		VmaAllocation allocation{VK_NULL_HANDLE};
		void *p_mapped_data{nullptr};
	};
	myvk::Ptr<myvk::Device> m_device_ptr;
	VkMemoryRequirements m_page_memory_requirements{};
	std::vector<Page> m_pages;
	std::size_t m_exist_page_total{};

public:
	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const override { return m_device_ptr; }

	~VkPagedBuffer() override;

	inline uint32_t GetPageTotal() const { return m_pages.size(); }
	inline uint32_t GetExistPageTotal() const { return m_exist_page_total; }
	inline const VkMemoryRequirements &GetPageMemoryRequirements() const { return m_page_memory_requirements; }
	inline VkDeviceSize GetPageSize() const { return m_page_memory_requirements.size; }

	inline bool IsPageExist(uint32_t page_id) const {
		assert(page_id < m_pages.size());
		return m_pages[page_id].allocation != VK_NULL_HANDLE;
	}
	template <typename T> inline T *GetMappedPage(uint32_t page_id) const {
		assert(page_id < m_pages.size());
		return (T *)m_pages[page_id].p_mapped_data;
	}

	inline static myvk::Ptr<VkPagedBuffer> Create(const myvk::Ptr<myvk::Device> &device, VkDeviceSize size,
	                                              std::invocable<const VkMemoryRequirements &> auto &&get_page_size,
	                                              VkBufferUsageFlags usages,
	                                              const std::vector<myvk::Ptr<myvk::Queue>> &access_queues = {}) {
		VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		create_info.size = size;
		create_info.usage = usages;
		create_info.flags = VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT | VK_BUFFER_CREATE_SPARSE_BINDING_BIT;

		std::set<uint32_t> queue_family_set;
		for (auto &i : access_queues)
			queue_family_set.insert(i->GetFamilyIndex());
		std::vector<uint32_t> queue_families{queue_family_set.begin(), queue_family_set.end()};

		if (queue_families.size() <= 1)
			create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		else {
			create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
			create_info.queueFamilyIndexCount = queue_families.size();
			create_info.pQueueFamilyIndices = queue_families.data();
		}

		VkBuffer buffer;

		if (vkCreateBuffer(device->GetHandle(), &create_info, nullptr, &buffer) != VK_SUCCESS)
			return nullptr;

		VkMemoryRequirements page_memory_requirements{};
		vkGetBufferMemoryRequirements(device->GetHandle(), buffer, &page_memory_requirements);
		page_memory_requirements.size = get_page_size(page_memory_requirements);

		if (page_memory_requirements.size == 0)
			return nullptr;

		uint32_t page_total = (size + page_memory_requirements.size - 1) / page_memory_requirements.size;

		auto ret = myvk::MakePtr<VkPagedBuffer>();
		ret->m_buffer = buffer;
		ret->m_size = size;
		ret->m_usage = usages;

		ret->m_device_ptr = device;
		ret->m_page_memory_requirements = page_memory_requirements;
		ret->m_pages.resize(page_total);
		return ret;
	}

	inline void Alloc(const myvk::Ptr<VkSparseBinder> &binder, std::ranges::input_range auto &&page_ids) {
		if (page_ids.empty())
			return;

		m_exist_page_total += page_ids.size();
		std::vector<VmaAllocation> allocations(page_ids.size());
		std::vector<VmaAllocationInfo> allocation_infos(page_ids.size());

		VmaAllocationCreateInfo create_info = {};
		create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		vmaAllocateMemoryPages(m_device_ptr->GetAllocatorHandle(), &m_page_memory_requirements, &create_info,
		                       page_ids.size(), allocations.data(), allocation_infos.data());

		std::vector<VkSparseMemoryBind> sparse_memory_binds;
		sparse_memory_binds.reserve(page_ids.size());

		for (uint32_t counter = 0; uint32_t page_id : page_ids) {
			assert(!IsPageExist(page_id));

			const auto &allocation_info = allocation_infos[counter];

			m_pages[page_id] = {
			    .allocation = allocations[counter],
			    .p_mapped_data = (uint32_t *)allocation_info.pMappedData,
			};

			VkDeviceSize resource_offset = page_id * m_page_memory_requirements.size;
			sparse_memory_binds.push_back({
			    .resourceOffset = resource_offset,
			    .size = std::min(m_page_memory_requirements.size, m_size - resource_offset),
			    .memory = allocation_info.deviceMemory,
			    .memoryOffset = allocation_info.offset,
			    .flags = 0,
			});

			++counter;
		}

		binder->Push(std::static_pointer_cast<BufferBase>(shared_from_this()), sparse_memory_binds);
	}

	inline void Free(const myvk::Ptr<VkSparseBinder> &binder, std::ranges::input_range auto &&page_ids) {
		if (page_ids.empty())
			return;

		m_exist_page_total -= page_ids.size();
		std::vector<VkSparseMemoryBind> sparse_memory_binds;
		sparse_memory_binds.reserve(page_ids.size());

		std::vector<VmaAllocation> allocations;
		for (uint32_t page_id : page_ids) {
			assert(IsPageExist(page_id));

			allocations.push_back(m_pages[page_id].allocation);
			m_pages[page_id] = {
			    .allocation = VK_NULL_HANDLE,
			    .p_mapped_data = nullptr,
			};

			VkDeviceSize resource_offset = page_id * m_page_memory_requirements.size;
			sparse_memory_binds.push_back({
			    .resourceOffset = resource_offset,
			    .size = std::min(m_page_memory_requirements.size, m_size - resource_offset),
			    .memory = VK_NULL_HANDLE,
			});
		}
		vmaFreeMemoryPages(m_device_ptr->GetAllocatorHandle(), allocations.size(), allocations.data());

		binder->Push(std::static_pointer_cast<BufferBase>(shared_from_this()), sparse_memory_binds);
	}
};

#endif // VKPAGEDBUFFER_HPP

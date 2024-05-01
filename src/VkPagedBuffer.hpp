//
// Created by adamyuan on 4/30/24.
//

#pragma once
#ifndef VKPAGEDBUFFER_HPP
#define VKPAGEDBUFFER_HPP

#include <concepts>
#include <set>

#include <myvk/BufferBase.hpp>
#include <myvk/Queue.hpp>

class VkPagedBuffer final : public myvk::BufferBase {
public:
	struct Page {
		VmaAllocation allocation;
		void *p_mapped_data;
	};

private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	VkMemoryRequirements m_page_memory_requirements{};
	uint32_t m_page_count{};
	std::vector<Page> m_pages;

public:
	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const override { return m_device_ptr; }

	inline uint32_t GetPageCount() const { return m_page_count; }
	inline const VkMemoryRequirements &GetPageMemoryRequirements() const { return m_page_memory_requirements; }
	inline VkDeviceSize GetPageSize() const { return m_page_memory_requirements.size; }

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

		uint32_t page_count = (size + page_memory_requirements.size - 1) / page_memory_requirements.size;

		auto ret = myvk::MakePtr<VkPagedBuffer>();
		ret->m_buffer = buffer;
		ret->m_size = size;
		ret->m_usage = usages;

		ret->m_device_ptr = device;
		ret->m_page_memory_requirements = page_memory_requirements;
		ret->m_page_count = page_count;
		ret->m_pages.resize(page_count);
	}
};

#endif // VKPAGEDBUFFER_HPP

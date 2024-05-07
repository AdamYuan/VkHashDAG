//
// Created by adamyuan on 5/7/24.
//

#pragma once
#ifndef HASHDAG_VKSPARSEBINDER_HPP
#define HASHDAG_VKSPARSEBINDER_HPP

#include <span>
#include <unordered_map>
#include <vector>

#include <myvk/BufferBase.hpp>
#include <myvk/Fence.hpp>
#include <myvk/Queue.hpp>
#include <myvk/Semaphore.hpp>

class VkSparseBinder final : public myvk::DeviceObjectBase {
	myvk::Ptr<myvk::Queue> m_sparse_queue_ptr;

	std::unordered_map<myvk::Ptr<myvk::BufferBase>, std::vector<VkSparseMemoryBind>> m_entries;

public:
	inline explicit VkSparseBinder(myvk::Ptr<myvk::Queue> sparse_queue) : m_sparse_queue_ptr{std::move(sparse_queue)} {}
	inline ~VkSparseBinder() override = default;

	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const override { return m_sparse_queue_ptr->GetDevicePtr(); }

	inline void Push(const myvk::Ptr<myvk::BufferBase> &buffer, std::span<const VkSparseMemoryBind> binds) {
		if (!binds.empty()) {
			std::vector<VkSparseMemoryBind> &vec = m_entries[buffer];
			vec.insert(vec.end(), binds.begin(), binds.end());
		}
	}

	VkResult QueueBind(const myvk::SemaphoreGroup &wait_semaphores, const myvk::SemaphoreGroup &signal_semaphores,
	                   const myvk::Ptr<myvk::Fence> &fence);
};

#endif

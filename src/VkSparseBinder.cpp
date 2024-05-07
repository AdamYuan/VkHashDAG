//
// Created by adamyuan on 5/7/24.
//

#include "VkSparseBinder.hpp"

VkResult VkSparseBinder::QueueBind(const myvk::SemaphoreGroup &wait_semaphores,
                                   const myvk::SemaphoreGroup &signal_semaphores, const myvk::Ptr<myvk::Fence> &fence) {
	if (m_entries.empty())
		return VkResult::VK_NOT_READY;

	VkBindSparseInfo bind_sparse_info = {VK_STRUCTURE_TYPE_BIND_SPARSE_INFO};
	bind_sparse_info.waitSemaphoreCount = wait_semaphores.GetCount();
	bind_sparse_info.pWaitSemaphores = wait_semaphores.GetSemaphoresPtr();
	bind_sparse_info.signalSemaphoreCount = signal_semaphores.GetCount();
	bind_sparse_info.pSignalSemaphores = signal_semaphores.GetSemaphoresPtr();

	std::vector<VkSparseBufferMemoryBindInfo> sparse_buffer_memory_bind_infos;
	for (const auto &[buffer, binds] : m_entries)
		sparse_buffer_memory_bind_infos.push_back({
		    .buffer = buffer->GetHandle(),
		    .bindCount = (uint32_t)binds.size(),
		    .pBinds = binds.data(),
		});

	bind_sparse_info.bufferBindCount = sparse_buffer_memory_bind_infos.size();
	bind_sparse_info.pBufferBinds = sparse_buffer_memory_bind_infos.data();

	VkResult result = vkQueueBindSparse(m_sparse_queue_ptr->GetHandle(), 1, &bind_sparse_info,
	                                    fence ? fence->GetHandle() : VK_NULL_HANDLE);
	m_entries.clear();
	return result;
}
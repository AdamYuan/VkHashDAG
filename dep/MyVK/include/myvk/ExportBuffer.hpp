#pragma once
#ifndef MYVK_EXPORT_BUFFER_HPP
#define MYVK_EXPORT_BUFFER_HPP

#include "BufferBase.hpp"

namespace myvk {

class ExportBuffer final : public BufferBase {
public:
	struct Handle {
		VkBuffer buffer{VK_NULL_HANDLE};
		VkDeviceMemory device_memory{VK_NULL_HANDLE};
		void *mem_handle{nullptr};
		bool IsValid() const { return buffer && device_memory && mem_handle; }
		explicit operator bool() const { return IsValid(); }
	};
	static Handle CreateHandle(const Ptr<Device> &device, VkDeviceSize size, VkBufferUsageFlags usage,
	                           VkMemoryPropertyFlags memory_properties,
	                           const std::vector<Ptr<Queue>> &access_queues = {});
	static void DestroyHandle(const Ptr<Device> &device, Handle handle);

private:
	Ptr<Device> m_device_ptr;
	VkDeviceMemory m_device_memory{VK_NULL_HANDLE};
	void *m_mem_handle{nullptr};

public:
	~ExportBuffer() override;
	static Ptr<ExportBuffer> Create(const Ptr<Device> &device, VkDeviceSize size, VkBufferUsageFlags usage,
	                                VkMemoryPropertyFlags memory_properties,
	                                const std::vector<Ptr<Queue>> &access_queues = {});
	inline void *GetMemoryHandle() const { return m_mem_handle; }

	inline const Ptr<Device> &GetDevicePtr() const override { return m_device_ptr; }
};

} // namespace myvk

#endif

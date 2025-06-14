#ifndef MYVK_BUFFER_BASE_HPP
#define MYVK_BUFFER_BASE_HPP

#include "DeviceObjectBase.hpp"
#include "SyncHelper.hpp"

namespace myvk {
struct BufferSubresourceRange {
	VkDeviceSize offset, size;
};

class BufferBase : public DeviceObjectBase {
protected:
	VkBuffer m_buffer{VK_NULL_HANDLE};
	VkBufferUsageFlags m_usage{};
	VkDeviceSize m_size{0};

public:
	VkBuffer GetHandle() const { return m_buffer; }

	VkDeviceSize GetSize() const { return m_size; }
	VkBufferUsageFlags GetUsage() const { return m_usage; }

	~BufferBase() override = default;

	BufferSubresourceRange GetSubresourceRange() const { return {.offset = 0, .size = m_size}; }
	static BufferSubresourceRange GetCopySrcSubresourceRange(const VkBufferCopy &copy) {
		return {.offset = copy.srcOffset, .size = copy.size};
	}
	static BufferSubresourceRange GetCopyDstSubresourceRange(const VkBufferCopy &copy) {
		return {.offset = copy.dstOffset, .size = copy.size};
	}
	BufferSubresourceRange GetCopySubresourceRange(const VkBufferImageCopy &copy) const {
		// TODO: This is not precise
		return {.offset = copy.bufferOffset, .size = m_size - copy.bufferOffset};
	}

	std::vector<VkBufferMemoryBarrier> GetMemoryBarriers(const std::vector<BufferSubresourceRange> &regions,
	                                                     VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
	                                                     uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                                     uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	std::vector<VkBufferMemoryBarrier> GetSrcMemoryBarriers(const std::vector<VkBufferCopy> &regions,
	                                                        VkAccessFlags src_access_mask,
	                                                        VkAccessFlags dst_access_mask,
	                                                        uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                                        uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	std::vector<VkBufferMemoryBarrier> GetDstMemoryBarriers(const std::vector<VkBufferCopy> &regions,
	                                                        VkAccessFlags src_access_mask,
	                                                        VkAccessFlags dst_access_mask,
	                                                        uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                                        uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	std::vector<VkBufferMemoryBarrier> GetSrcMemoryBarriers(const std::vector<VkBufferImageCopy> &regions,
	                                                        VkAccessFlags src_access_mask,
	                                                        VkAccessFlags dst_access_mask,
	                                                        uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                                        uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkBufferMemoryBarrier GetMemoryBarrier(VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
	                                       uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                       uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkBufferMemoryBarrier GetMemoryBarrier(const BufferSubresourceRange &region, VkAccessFlags src_access_mask,
	                                       VkAccessFlags dst_access_mask,
	                                       uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                       uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkBufferMemoryBarrier GetSrcMemoryBarrier(const VkBufferCopy &region, VkAccessFlags src_access_mask,
	                                          VkAccessFlags dst_access_mask,
	                                          uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                          uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkBufferMemoryBarrier GetDstMemoryBarrier(const VkBufferCopy &region, VkAccessFlags src_access_mask,
	                                          VkAccessFlags dst_access_mask,
	                                          uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                          uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkBufferMemoryBarrier2 GetMemoryBarrier2(const BufferSubresourceRange &region, //
	                                         VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask,
	                                         VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask,
	                                         uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                         uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return VkBufferMemoryBarrier2{
		    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
		    .srcStageMask = src_stage_mask,
		    .srcAccessMask = src_access_mask,
		    .dstStageMask = dst_stage_mask,
		    .dstAccessMask = dst_access_mask,
		    .srcQueueFamilyIndex = src_queue_family,
		    .dstQueueFamilyIndex = dst_queue_family,
		    .buffer = m_buffer,
		    .offset = region.offset,
		    .size = region.size,
		};
	}

	VkBufferMemoryBarrier2 GetMemoryBarrier2(VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask,
	                                         VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask,
	                                         uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                         uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return GetMemoryBarrier2(GetSubresourceRange(), src_stage_mask, src_access_mask, dst_stage_mask,
		                         dst_access_mask, src_queue_family, dst_queue_family);
	}

	VkBufferMemoryBarrier2 GetMemoryBarrier2(const BufferSubresourceRange &region, //
	                                         const BufferSyncState &src_sync_state,
	                                         const BufferSyncState &dst_sync_state,
	                                         uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                         uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return GetMemoryBarrier2(region,                                                //
		                         src_sync_state.stage_mask, src_sync_state.access_mask, //
		                         dst_sync_state.stage_mask, dst_sync_state.access_mask, //
		                         src_queue_family, dst_queue_family);
	}

	VkBufferMemoryBarrier2 GetMemoryBarrier2(const BufferSyncState &src_sync_state,
	                                         const BufferSyncState &dst_sync_state,
	                                         uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                         uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return GetMemoryBarrier2(src_sync_state.stage_mask, src_sync_state.access_mask, //
		                         dst_sync_state.stage_mask, dst_sync_state.access_mask, //
		                         src_queue_family, dst_queue_family);
	}
};
} // namespace myvk

#endif

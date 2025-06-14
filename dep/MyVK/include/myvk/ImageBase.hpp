#ifndef MYVK_IMAGE_BASE_HPP
#define MYVK_IMAGE_BASE_HPP

#include "DeviceObjectBase.hpp"
#include "SyncHelper.hpp"

namespace myvk {
class ImageBase : public DeviceObjectBase {
private:
	static inline uint32_t simple_ctz(uint32_t x) {
		if (x & 0x80000000u)
			return 32u;
		uint32_t ret{1};
		while (x >> ret)
			++ret;
		return ret;
	}

protected:
	VkImage m_image{VK_NULL_HANDLE};

	// TODO: VkImageCreateFlags ?
	VkImageUsageFlags m_usage{};
	VkExtent3D m_extent{};
	VkImageType m_type{};
	VkFormat m_format{};
	uint32_t m_mip_levels{};
	uint32_t m_array_layers{};

public:
	~ImageBase() override = default;

	VkImage GetHandle() const { return m_image; }

	const VkExtent3D &GetExtent() const { return m_extent; }
	VkExtent2D GetExtent2D() const { return {m_extent.width, m_extent.height}; }

	VkImageUsageFlags GetUsage() const { return m_usage; }

	VkImageType GetType() const { return m_type; }

	VkFormat GetFormat() const { return m_format; }

	uint32_t GetMipLevels() const { return m_mip_levels; }

	uint32_t GetArrayLayers() const { return m_array_layers; }

	VkImageAspectFlags GetAllAspects() const { return GetFormatImageAspects(m_format); }

	inline static uint32_t QueryMipLevel(uint32_t w) { return simple_ctz(w); }
	inline static uint32_t QueryMipLevel(const VkExtent2D &size) { return simple_ctz(size.width | size.height); }
	inline static uint32_t QueryMipLevel(const VkExtent3D &size) {
		return simple_ctz(size.width | size.height | size.depth);
	}

	VkImageSubresourceRange GetSubresourceRange(VkImageAspectFlags aspect_mask) const {
		return {aspect_mask, 0, m_mip_levels, 0, m_array_layers};
	}
	static VkImageSubresourceRange GetCopySubresourceRange(const VkBufferImageCopy &copy) {
		return {copy.imageSubresource.aspectMask, copy.imageSubresource.mipLevel, 1,
		        copy.imageSubresource.baseArrayLayer, copy.imageSubresource.layerCount};
	}
	static VkImageSubresourceRange GetCopySrcSubresourceRange(const VkImageCopy &copy) {
		return {copy.srcSubresource.aspectMask, copy.srcSubresource.mipLevel, 1, copy.srcSubresource.baseArrayLayer,
		        copy.srcSubresource.layerCount};
	}
	static VkImageSubresourceRange GetCopyDstSubresourceRange(const VkImageCopy &copy) {
		return {copy.dstSubresource.aspectMask, copy.dstSubresource.mipLevel, 1, copy.dstSubresource.baseArrayLayer,
		        copy.dstSubresource.layerCount};
	}

	std::vector<VkImageMemoryBarrier> GetDstMemoryBarriers(const std::vector<VkBufferImageCopy> &regions,
	                                                       VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
	                                                       VkImageLayout old_layout, VkImageLayout new_layout,
	                                                       uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                                       uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	std::vector<VkImageMemoryBarrier> GetMemoryBarriers(const std::vector<VkImageSubresourceLayers> &regions,
	                                                    VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
	                                                    VkImageLayout old_layout, VkImageLayout new_layout,
	                                                    uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                                    uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	std::vector<VkImageMemoryBarrier> GetMemoryBarriers(const std::vector<VkImageSubresourceRange> &regions,
	                                                    VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
	                                                    VkImageLayout old_layout, VkImageLayout new_layout,
	                                                    uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                                    uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkImageMemoryBarrier GetMemoryBarrier(VkImageAspectFlags aspect_mask, VkAccessFlags src_access_mask,
	                                      VkAccessFlags dst_access_mask, VkImageLayout old_layout,
	                                      VkImageLayout new_layout, uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                      uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkImageMemoryBarrier GetDstMemoryBarrier(const VkBufferImageCopy &region, VkAccessFlags src_access_mask,
	                                         VkAccessFlags dst_access_mask, VkImageLayout old_layout,
	                                         VkImageLayout new_layout,
	                                         uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                         uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkImageMemoryBarrier GetMemoryBarrier(const VkImageSubresourceLayers &region, VkAccessFlags src_access_mask,
	                                      VkAccessFlags dst_access_mask, VkImageLayout old_layout,
	                                      VkImageLayout new_layout, uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                      uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkImageMemoryBarrier GetMemoryBarrier(const VkImageSubresourceRange &region, VkAccessFlags src_access_mask,
	                                      VkAccessFlags dst_access_mask, VkImageLayout old_layout,
	                                      VkImageLayout new_layout, uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                      uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const;

	VkImageMemoryBarrier2 GetMemoryBarrier2(const VkImageSubresourceRange &region, //
	                                        VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask,
	                                        VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask,
	                                        VkImageLayout old_layout, VkImageLayout new_layout,
	                                        uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                        uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return VkImageMemoryBarrier2{
		    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		    .srcStageMask = src_stage_mask,
		    .srcAccessMask = src_access_mask,
		    .dstStageMask = dst_stage_mask,
		    .dstAccessMask = dst_access_mask,
		    .oldLayout = old_layout,
		    .newLayout = new_layout,
		    .srcQueueFamilyIndex = src_queue_family,
		    .dstQueueFamilyIndex = dst_queue_family,
		    .image = m_image,
		    .subresourceRange = region,
		};
	}

	VkImageMemoryBarrier2 GetMemoryBarrier2(VkImageAspectFlags aspect_mask, //
	                                        VkPipelineStageFlags2 src_stage_mask, VkAccessFlags2 src_access_mask,
	                                        VkPipelineStageFlags2 dst_stage_mask, VkAccessFlags2 dst_access_mask,
	                                        VkImageLayout old_layout, VkImageLayout new_layout,
	                                        uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                        uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return GetMemoryBarrier2(GetSubresourceRange(aspect_mask), src_stage_mask, src_access_mask, dst_stage_mask,
		                         dst_access_mask, old_layout, new_layout, src_queue_family, dst_queue_family);
	}

	VkImageMemoryBarrier2 GetMemoryBarrier2(const VkImageSubresourceRange &region, //
	                                        const ImageSyncState &src_sync_state, const ImageSyncState &dst_sync_state,
	                                        uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                        uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return GetMemoryBarrier2(region,                                                //
		                         src_sync_state.stage_mask, src_sync_state.access_mask, //
		                         dst_sync_state.stage_mask, dst_sync_state.access_mask, //
		                         src_sync_state.layout, dst_sync_state.layout,          //
		                         src_queue_family, dst_queue_family);
	}

	VkImageMemoryBarrier2 GetMemoryBarrier2(VkImageAspectFlags aspect_mask, //
	                                        const ImageSyncState &src_sync_state, const ImageSyncState &dst_sync_state,
	                                        uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                        uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return GetMemoryBarrier2(aspect_mask,                                           //
		                         src_sync_state.stage_mask, src_sync_state.access_mask, //
		                         dst_sync_state.stage_mask, dst_sync_state.access_mask, //
		                         src_sync_state.layout, dst_sync_state.layout,          //
		                         src_queue_family, dst_queue_family);
	}

	VkImageMemoryBarrier2 GetMemoryBarrier2(const ImageSyncState &src_sync_state, const ImageSyncState &dst_sync_state,
	                                        uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED,
	                                        uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED) const {
		return GetMemoryBarrier2(GetAllAspects(), src_sync_state, dst_sync_state, src_queue_family, dst_queue_family);
	}

	inline VkFramebufferAttachmentImageInfo GetFramebufferAttachmentImageInfo() const {
		VkFramebufferAttachmentImageInfo ret = {VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO};
		ret.usage = m_usage;
		ret.width = m_extent.width;
		ret.height = m_extent.height;
		ret.layerCount = m_array_layers;
		ret.viewFormatCount = 1;
		ret.pViewFormats = &m_format;
		return ret;
	}
};
} // namespace myvk

#endif

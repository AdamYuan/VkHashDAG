#pragma once
#ifndef MYVK_BARRIERHELPER_HPP
#define MYVK_BARRIERHELPER_HPP

#include "volk.h"

namespace myvk {

// TODO: This function should not be placed in SyncHelper.hpp
inline constexpr VkImageAspectFlags GetFormatImageAspects(VkFormat format) {
	switch (format) {
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

inline constexpr VkAccessFlags2 GetWriteAccessMask2(VkAccessFlags2 access_mask) {
	constexpr VkAccessFlags2 kReadonlyAccessMask =
	    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
	    VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT |
	    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
	    VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_MEMORY_READ_BIT |
	    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
	    VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR | // VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR | // Vulkan 1.4
	    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_INDEX_READ_BIT_KHR |
	    VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT_KHR | VK_ACCESS_2_UNIFORM_READ_BIT_KHR |
	    VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR |
	    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT_KHR |
	    VK_ACCESS_2_TRANSFER_READ_BIT_KHR | VK_ACCESS_2_HOST_READ_BIT_KHR | VK_ACCESS_2_MEMORY_READ_BIT_KHR |
	    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT_KHR | VK_ACCESS_2_SHADER_STORAGE_READ_BIT_KHR |
	    VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT | VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT |
	    VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_NV | // VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT | // Vulkan 1.4
	    VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_SHADING_RATE_IMAGE_READ_BIT_NV |
	    VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_NV |
	    VK_ACCESS_2_FRAGMENT_DENSITY_MAP_READ_BIT_EXT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT |
	    VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT | VK_ACCESS_2_INVOCATION_MASK_READ_BIT_HUAWEI |
	    VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR | VK_ACCESS_2_MICROMAP_READ_BIT_EXT |
	    VK_ACCESS_2_OPTICAL_FLOW_READ_BIT_NV;
	return access_mask & (~kReadonlyAccessMask);
}

inline constexpr bool IsAccessMaskReadOnly2(VkAccessFlags2 access_mask) {
	return GetWriteAccessMask2(access_mask) == 0;
}

struct MemorySyncState {
	VkPipelineStageFlags2 stage_mask{VK_PIPELINE_STAGE_2_NONE};
	VkAccessFlags2 access_mask{VK_ACCESS_2_NONE};

	constexpr bool operator==(const MemorySyncState &) const = default;
	constexpr MemorySyncState &operator|=(const MemorySyncState &r) {
		stage_mask |= r.stage_mask;
		access_mask |= r.access_mask;
		return *this;
	}
	constexpr MemorySyncState operator|(const MemorySyncState &r) const {
		auto l = *this;
		l |= r;
		return l;
	}
	constexpr MemorySyncState GetWrite() const { return {stage_mask, GetWriteAccessMask2(access_mask)}; }
};

struct BufferSyncState {
	VkPipelineStageFlags2 stage_mask{VK_PIPELINE_STAGE_2_NONE};
	VkAccessFlags2 access_mask{VK_ACCESS_2_NONE};

	constexpr bool operator==(const BufferSyncState &) const = default;
	constexpr BufferSyncState &operator|=(const BufferSyncState &r) {
		stage_mask |= r.stage_mask;
		access_mask |= r.access_mask;
		return *this;
	}
	constexpr BufferSyncState operator|(const BufferSyncState &r) const {
		auto l = *this;
		l |= r;
		return l;
	}
	constexpr BufferSyncState GetWrite() const { return {stage_mask, GetWriteAccessMask2(access_mask)}; }
};

struct ImageSyncState {
	VkPipelineStageFlags2 stage_mask{VK_PIPELINE_STAGE_2_NONE};
	VkAccessFlags2 access_mask{VK_ACCESS_2_NONE};
	VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};

	constexpr bool operator==(const ImageSyncState &) const = default;
	constexpr ImageSyncState &operator|=(const ImageSyncState &r) {
		stage_mask |= r.stage_mask;
		access_mask |= r.access_mask;
		// assert(layout == VK_IMAGE_LAYOUT_UNDEFINED || r.layout == VK_IMAGE_LAYOUT_UNDEFINED || layout == r.layout);
		if (layout == VK_IMAGE_LAYOUT_UNDEFINED)
			layout = r.layout;
		return *this;
	}
	constexpr ImageSyncState operator|(const ImageSyncState &r) const {
		auto l = *this;
		l |= r;
		return l;
	}
	constexpr ImageSyncState GetWrite() const { return {stage_mask, GetWriteAccessMask2(access_mask), layout}; }
};

namespace concepts {
template <typename T>
concept SyncState =
    std::same_as<T, MemorySyncState> || std::same_as<T, BufferSyncState> || std::same_as<T, ImageSyncState>;
}

template <concepts::SyncState To_T, concepts::SyncState From_T> constexpr To_T SyncStateCast(const From_T &from) {
	if constexpr (std::same_as<From_T, To_T>)
		return from;
	else {
		return To_T{
		    .stage_mask = from.stage_mask,
		    .access_mask = from.access_mask,
		};
	}
}
template <concepts::SyncState From_T>
constexpr ImageSyncState SyncStateCast(const From_T &from, VkImageLayout layout)
requires(!std::same_as<From_T, ImageSyncState>)
{
	return ImageSyncState{
	    .stage_mask = from.stage_mask,
	    .access_mask = from.access_mask,
	    .layout = layout,
	};
}

// Spec 8.5. Render Pass Load Operations
inline constexpr VkPipelineStageFlags2 GetAttachmentLoadOpStageMask2(VkImageAspectFlags aspects,
                                                                     VkAttachmentLoadOp load_op) {
	VkPipelineStageFlags2 ret{};
	if (load_op != VK_ATTACHMENT_LOAD_OP_NONE_EXT) {
		if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
			ret |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
			ret |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
	}
	return ret;
}
inline constexpr VkPipelineStageFlags2 GetAttachmentLoadOpStageMask2(VkImageAspectFlags aspects,
                                                                     VkAttachmentLoadOp load_op,
                                                                     VkAttachmentLoadOp stencil_load_op) {
	return GetAttachmentLoadOpStageMask2(aspects & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT), load_op) |
	       GetAttachmentLoadOpStageMask2(aspects & VK_IMAGE_ASPECT_STENCIL_BIT, stencil_load_op);
}
inline constexpr VkAccessFlags2 GetAttachmentLoadOpAccessMask2(VkImageAspectFlags aspects, VkAttachmentLoadOp load_op) {
	switch (load_op) {
	case VK_ATTACHMENT_LOAD_OP_LOAD: {
		VkAccessFlags2 ret{};
		if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
			ret |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
		if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
			ret |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		return ret;
	}
	case VK_ATTACHMENT_LOAD_OP_DONT_CARE:
	case VK_ATTACHMENT_LOAD_OP_CLEAR: {
		VkAccessFlags2 ret{};
		if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
			ret |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
			ret |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		return ret;
	}
	default:
		return {};
	}
}
inline constexpr VkAccessFlags2 GetAttachmentLoadOpAccessMask2(VkImageAspectFlags aspects, VkAttachmentLoadOp load_op,
                                                               VkAttachmentLoadOp stencil_load_op) {
	return GetAttachmentLoadOpAccessMask2(aspects & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT), load_op) |
	       GetAttachmentLoadOpAccessMask2(aspects & VK_IMAGE_ASPECT_STENCIL_BIT, stencil_load_op);
}

// Spec 8.6. Render Pass Store Operations
inline constexpr VkPipelineStageFlags2 GetAttachmentStoreOpStageMask2(VkImageAspectFlags aspects,
                                                                      VkAttachmentStoreOp store_op) {
	VkPipelineStageFlags2 ret{};
	if (store_op != VK_ATTACHMENT_STORE_OP_NONE) {
		if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
			ret |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
			ret |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	}
	return ret;
}
inline constexpr VkPipelineStageFlags2 GetAttachmentStoreOpStageMask2(VkImageAspectFlags aspects,
                                                                      VkAttachmentStoreOp store_op,
                                                                      VkAttachmentStoreOp stencil_store_op) {
	return GetAttachmentStoreOpStageMask2(aspects & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT), store_op) |
	       GetAttachmentStoreOpStageMask2(aspects & VK_IMAGE_ASPECT_STENCIL_BIT, stencil_store_op);
}
inline constexpr VkAccessFlags2 GetAttachmentStoreOpAccessMask2(VkImageAspectFlags aspects,
                                                                VkAttachmentStoreOp store_op) {
	switch (store_op) {
	case VK_ATTACHMENT_STORE_OP_DONT_CARE:
	case VK_ATTACHMENT_STORE_OP_STORE: {
		VkAccessFlags2 ret{};
		if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
			ret |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
			ret |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		return ret;
	}
	case VK_ATTACHMENT_STORE_OP_NONE:
	default:
		return {};
	}
}
inline constexpr VkAccessFlags2 GetAttachmentStoreOpAccessMask2(VkImageAspectFlags aspects,
                                                                VkAttachmentStoreOp store_op,
                                                                VkAttachmentStoreOp stencil_store_op) {
	return GetAttachmentStoreOpAccessMask2(aspects & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT),
	                                       store_op) |
	       GetAttachmentStoreOpAccessMask2(aspects & VK_IMAGE_ASPECT_STENCIL_BIT, stencil_store_op);
}

// Spec 8.7. Render Pass Multisample Resolve Operations
inline constexpr VkPipelineStageFlags2 GetAttachmentResolveStageMask2() {
	return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
}
// Multi-Sample Src
inline constexpr VkAccessFlags2 GetAttachmentResolveSrcAccessMask2() { return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT; }
// Single-Sample Dst
inline constexpr VkAccessFlags2 GetAttachmentResolveDstAccessMask2() {
	return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
}

// MemorySyncState functions, for Subpass Dependency

inline constexpr MemorySyncState GetAttachmentLoadOpSync(VkImageAspectFlags aspects, VkAttachmentLoadOp load_op) {
	return {.stage_mask = GetAttachmentLoadOpStageMask2(aspects, load_op),
	        .access_mask = GetAttachmentLoadOpAccessMask2(aspects, load_op)};
}
inline constexpr MemorySyncState GetAttachmentLoadOpSync(VkFormat format, VkAttachmentLoadOp load_op) {
	return GetAttachmentLoadOpSync(GetFormatImageAspects(format), load_op);
}
inline constexpr MemorySyncState GetAttachmentLoadOpSync(VkImageAspectFlags aspects, VkAttachmentLoadOp load_op,
                                                         VkAttachmentLoadOp stencil_load_op) {
	return {.stage_mask = GetAttachmentLoadOpStageMask2(aspects, load_op, stencil_load_op),
	        .access_mask = GetAttachmentLoadOpAccessMask2(aspects, load_op, stencil_load_op)};
}
inline constexpr MemorySyncState GetAttachmentLoadOpSync(VkFormat format, VkAttachmentLoadOp load_op,
                                                         VkAttachmentLoadOp stencil_load_op) {
	return GetAttachmentLoadOpSync(GetFormatImageAspects(format), load_op, stencil_load_op);
}

inline constexpr MemorySyncState GetAttachmentStoreOpSync(VkImageAspectFlags aspects, VkAttachmentStoreOp store_op) {
	return {.stage_mask = GetAttachmentStoreOpStageMask2(aspects, store_op),
	        .access_mask = GetAttachmentStoreOpAccessMask2(aspects, store_op)};
}
inline constexpr MemorySyncState GetAttachmentStoreOpSync(VkFormat format, VkAttachmentStoreOp store_op) {
	return GetAttachmentStoreOpSync(GetFormatImageAspects(format), store_op);
}
inline constexpr MemorySyncState GetAttachmentStoreOpSync(VkImageAspectFlags aspects, VkAttachmentStoreOp store_op,
                                                          VkAttachmentStoreOp stencil_store_op) {
	return {.stage_mask = GetAttachmentStoreOpStageMask2(aspects, store_op, stencil_store_op),
	        .access_mask = GetAttachmentStoreOpAccessMask2(aspects, store_op, stencil_store_op)};
}
inline constexpr MemorySyncState GetAttachmentStoreOpSync(VkFormat format, VkAttachmentStoreOp store_op,
                                                          VkAttachmentStoreOp stencil_store_op) {
	return GetAttachmentStoreOpSync(GetFormatImageAspects(format), store_op, stencil_store_op);
}
inline constexpr MemorySyncState GetAttachmentResolveSrcSync() {
	return {.stage_mask = GetAttachmentResolveStageMask2(), .access_mask = GetAttachmentResolveSrcAccessMask2()};
}
inline constexpr MemorySyncState GetAttachmentResolveDstSync() {
	return {.stage_mask = GetAttachmentResolveStageMask2(), .access_mask = GetAttachmentResolveDstAccessMask2()};
}

// ImageSyncState functions, for vkCmdPipelineBarrier2

inline constexpr ImageSyncState GetAttachmentLoadOpSync(VkImageAspectFlags aspects, VkAttachmentLoadOp load_op,
                                                        VkImageLayout initial_layout) {
	return {.stage_mask = GetAttachmentLoadOpStageMask2(aspects, load_op),
	        .access_mask = GetAttachmentLoadOpAccessMask2(aspects, load_op),
	        .layout = initial_layout};
}
inline constexpr ImageSyncState GetAttachmentLoadOpSync(VkFormat format, VkAttachmentLoadOp load_op,
                                                        VkImageLayout initial_layout) {
	return GetAttachmentLoadOpSync(GetFormatImageAspects(format), load_op, initial_layout);
}
inline constexpr ImageSyncState GetAttachmentLoadOpSync(VkImageAspectFlags aspects, VkAttachmentLoadOp load_op,
                                                        VkAttachmentLoadOp stencil_load_op,
                                                        VkImageLayout initial_layout) {
	return {.stage_mask = GetAttachmentLoadOpStageMask2(aspects, load_op, stencil_load_op),
	        .access_mask = GetAttachmentLoadOpAccessMask2(aspects, load_op, stencil_load_op),
	        .layout = initial_layout};
}
inline constexpr ImageSyncState GetAttachmentLoadOpSync(VkFormat format, VkAttachmentLoadOp load_op,
                                                        VkAttachmentLoadOp stencil_load_op,
                                                        VkImageLayout initial_layout) {
	return GetAttachmentLoadOpSync(GetFormatImageAspects(format), load_op, stencil_load_op, initial_layout);
}

inline constexpr ImageSyncState GetAttachmentStoreOpSync(VkImageAspectFlags aspects, VkAttachmentStoreOp store_op,
                                                         VkImageLayout final_layout) {
	return {.stage_mask = GetAttachmentStoreOpStageMask2(aspects, store_op),
	        .access_mask = GetAttachmentStoreOpAccessMask2(aspects, store_op),
	        .layout = final_layout};
}
inline constexpr ImageSyncState GetAttachmentStoreOpSync(VkFormat format, VkAttachmentStoreOp store_op,
                                                         VkImageLayout final_layout) {
	return GetAttachmentStoreOpSync(GetFormatImageAspects(format), store_op, final_layout);
}
inline constexpr ImageSyncState GetAttachmentStoreOpSync(VkImageAspectFlags aspects, VkAttachmentStoreOp store_op,
                                                         VkAttachmentStoreOp stencil_store_op,
                                                         VkImageLayout final_layout) {
	return {.stage_mask = GetAttachmentStoreOpStageMask2(aspects, store_op, stencil_store_op),
	        .access_mask = GetAttachmentStoreOpAccessMask2(aspects, store_op, stencil_store_op),
	        .layout = final_layout};
}
inline constexpr ImageSyncState GetAttachmentStoreOpSync(VkFormat format, VkAttachmentStoreOp store_op,
                                                         VkAttachmentStoreOp stencil_store_op,
                                                         VkImageLayout final_layout) {
	return GetAttachmentStoreOpSync(GetFormatImageAspects(format), store_op, stencil_store_op, final_layout);
}
inline constexpr ImageSyncState GetAttachmentResolveSrcSync(VkImageLayout layout) {
	return {.stage_mask = GetAttachmentResolveStageMask2(),
	        .access_mask = GetAttachmentResolveSrcAccessMask2(),
	        .layout = layout};
}
inline constexpr ImageSyncState GetAttachmentResolveDstSync(VkImageLayout layout) {
	return {.stage_mask = GetAttachmentResolveStageMask2(),
	        .access_mask = GetAttachmentResolveDstAccessMask2(),
	        .layout = layout};
}

} // namespace myvk

#endif

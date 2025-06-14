#ifndef MYVK_RENDER_PASS_HPP
#define MYVK_RENDER_PASS_HPP

#include "DeviceObjectBase.hpp"
#include "SyncHelper.hpp"
#include "volk.h"
#include <memory>

#include <map>
#include <optional>
#include <string>

namespace myvk {

class RenderPassState;
struct RenderPassState2;
class RenderPass : public DeviceObjectBase {
private:
	Ptr<Device> m_device_ptr;
	VkRenderPass m_render_pass{VK_NULL_HANDLE};

public:
	static Ptr<RenderPass> Create(const Ptr<Device> &device, const VkRenderPassCreateInfo &create_info);
	static Ptr<RenderPass> Create(const Ptr<Device> &device, const VkRenderPassCreateInfo2 &create_info);
	static Ptr<RenderPass> Create(const Ptr<Device> &device, const RenderPassState &state);
	static Ptr<RenderPass> Create(const Ptr<Device> &device, const RenderPassState2 &state);

	VkRenderPass GetHandle() const { return m_render_pass; }

	const Ptr<Device> &GetDevicePtr() const override { return m_device_ptr; }

	~RenderPass() override;
};

class [[deprecated]] RenderPassState {
public:
	class SubpassAttachmentHandle {
	private:
		RenderPassState *m_state_ptr;
		uint32_t m_subpass;

	public:
		SubpassAttachmentHandle(RenderPassState *state_ptr, uint32_t subpass)
		    : m_state_ptr{state_ptr}, m_subpass{subpass} {}
		SubpassAttachmentHandle
		AddColorResolveAttachment(const char *attachment_str, VkImageLayout layout, const char *resolve_attachment_str,
		                          VkImageLayout resolve_layout, const char *generator_subpass_str,
		                          VkPipelineStageFlags generator_stage, VkPipelineStageFlags use_stage,
		                          VkAccessFlags generator_access, VkAccessFlags use_access,
		                          VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT);
		inline SubpassAttachmentHandle
		AddColorAttachment(const char *attachment_str, VkImageLayout layout, const char *generator_subpass_str,
		                   VkPipelineStageFlags generator_stage, VkPipelineStageFlags use_stage,
		                   VkAccessFlags generator_access, VkAccessFlags use_access,
		                   VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT) {
			return AddColorResolveAttachment(attachment_str, layout, nullptr, VK_IMAGE_LAYOUT_UNDEFINED,
			                                 generator_subpass_str, generator_stage, use_stage, generator_access,
			                                 use_access, dependency_flag);
		}
		inline SubpassAttachmentHandle
		AddDefaultColorAttachment(const char *attachment_str, const char *generator_subpass_str,
		                          VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT) {
			return AddColorAttachment(
			    attachment_str, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, generator_subpass_str,
			    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, dependency_flag);
		}
		SubpassAttachmentHandle AddInputAttachment(const char *attachment_str, VkImageLayout layout,
		                                           const char *generator_subpass_str,
		                                           VkPipelineStageFlags generator_stage, VkPipelineStageFlags use_stage,
		                                           VkAccessFlags generator_access, VkAccessFlags use_access,
		                                           VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT);
		inline SubpassAttachmentHandle
		AddDefaultInputAttachment(const char *attachment_str, const char *generator_subpass_str,
		                          VkPipelineStageFlags generator_stage, VkPipelineStageFlags use_stage,
		                          VkAccessFlags generator_access, VkAccessFlags use_access,
		                          VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT) {
			return AddInputAttachment(attachment_str, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, generator_subpass_str,
			                          generator_stage, use_stage, generator_access, use_access, dependency_flag);
		}
		inline SubpassAttachmentHandle
		AddDefaultColorInputAttachment(const char *attachment_str, const char *generator_subpass_str,
		                               VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT) {
			return AddInputAttachment(attachment_str, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, generator_subpass_str,
			                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			                          VK_ACCESS_SHADER_READ_BIT, dependency_flag);
		}
		/* SubpassAttachmentHandle AddPreserveAttachment(const char *attachment_str, const char *generator_subpass_str,
		                                              VkPipelineStageFlags generator_stage,
		                                              VkPipelineStageFlags use_stage, VkAccessFlags generator_access,
		                                              VkAccessFlags use_access,
		                                              VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT);
		 */
		SubpassAttachmentHandle
		SetDepthStencilAttachment(const char *attachment_str, VkImageLayout layout, const char *src_subpass_str,
		                          VkPipelineStageFlags src_stage, VkPipelineStageFlags use_stage,
		                          VkAccessFlags src_access, VkAccessFlags use_access,
		                          VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT);
		inline SubpassAttachmentHandle
		SetReadOnlyDepthStencilAttachment(const char *attachment_str, const char *src_subpass_str,
		                                  VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT) {
			return SetDepthStencilAttachment(
			    attachment_str, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, src_subpass_str,
			    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, dependency_flag);
		}
		inline SubpassAttachmentHandle
		SetReadWriteDepthStencilAttachment(const char *attachment_str, const char *src_subpass_str,
		                                   VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT) {
			return SetDepthStencilAttachment(
			    attachment_str, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, src_subpass_str,
			    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			    dependency_flag);
		}
	};

private:
	std::map<std::string, uint32_t> m_subpass_map, m_attachment_map;
	uint32_t get_subpass(const char *subpass_str) const;
	uint32_t get_attachment(const char *attachment_str) const;
	std::vector<VkAttachmentDescription> m_attachment_descriptions;
	struct SubpassInfo {
		std::vector<VkAttachmentReference> color_attachment_references, input_attachment_references,
		    resolve_attachment_references;
		// TODO: preserve attachments?
		// std::vector<uint32_t> preserve_attachments;
		VkAttachmentReference depth_stencil_attachment_reference;
	};
	std::vector<SubpassInfo> m_subpass_infos;
	std::vector<VkSubpassDescription> m_subpass_descriptions;

	inline uint32_t get_subpass_count() const { return m_subpass_infos.size(); }
	inline uint32_t get_attachment_count() const { return m_attachment_descriptions.size(); }

	void insert_subpass_dependency(uint32_t src_subpass, uint32_t dst_subpass, VkPipelineStageFlags src_stage,
	                               VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access,
	                               VkDependencyFlags dependency_flag = VK_DEPENDENCY_BY_REGION_BIT);
	struct SubpassDependencyKey {
		uint32_t subpass_from, subpass_to;
		VkDependencyFlags dependency_flag;
		inline bool operator==(const SubpassDependencyKey &r) const {
			return subpass_from == r.subpass_from && subpass_to == r.subpass_to && dependency_flag == r.dependency_flag;
		}
		inline bool operator<(const SubpassDependencyKey &r) const {
			return std::tie(subpass_from, subpass_to, dependency_flag) <
			       std::tie(r.subpass_from, r.subpass_to, r.dependency_flag);
		}
	};
	std::map<SubpassDependencyKey, uint32_t> m_subpass_dependency_indices;
	std::vector<VkSubpassDependency> m_subpass_dependencies;

public:
	void Initialize(uint32_t subpass_count, uint32_t attachment_count);

	RenderPassState() = default;
	inline RenderPassState(uint32_t subpass_count, uint32_t attachment_count) {
		Initialize(subpass_count, attachment_count);
	}

	SubpassAttachmentHandle RegisterSubpass(uint32_t subpass, const char *subpass_str,
	                                        VkSubpassDescriptionFlags flags = 0);
	void RegisterAttachment(uint32_t attachment, const char *attachment_str, VkFormat format,
	                        VkImageLayout initial_layout, VkImageLayout final_layout, VkSampleCountFlagBits samples,
	                        VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op,
	                        VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	                        VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE);

	void AddExtraSubpassDependency(const char *src_subpass_str, const char *dst_subpass_str,
	                               VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
	                               VkAccessFlags src_access, VkAccessFlags dst_access,
	                               VkDependencyFlags dependency_flag);

	void PopRenderPassCreateInfo(VkRenderPassCreateInfo *info) const;
};

struct RenderPassState2 {
	struct SubpassInfo {
		struct InputAttachmentRef {
			uint32_t attachment;
			VkImageLayout layout;
			VkImageAspectFlags aspect_mask;
		};
		struct ColorAttachmentRef {
			uint32_t attachment{};
			VkImageLayout layout{};
			uint32_t resolveAttachment{VK_ATTACHMENT_UNUSED};
			VkImageLayout resolveLayout{};
		};
		struct AttachmentRef {
			uint32_t attachment;
			VkImageLayout layout;
		};
		VkPipelineBindPoint pipeline_bind_point{VK_PIPELINE_BIND_POINT_GRAPHICS};
		std::vector<InputAttachmentRef> input_attachment_refs;
		std::vector<ColorAttachmentRef> color_attachment_refs;
		std::optional<AttachmentRef> opt_depth_stencil_attachment_ref;
		std::vector<uint32_t> preserve_attachment_refs;
	};
	struct DependencyInfo {
		struct Sync {
			uint32_t subpass{};
			MemorySyncState sync{};
		} src{}, dst{};
		VkDependencyFlags dependency_flags{};
	};
	struct AttachmentInfo {
		VkFormat format{};
		VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
		struct Load {
			VkAttachmentLoadOp op{VK_ATTACHMENT_LOAD_OP_DONT_CARE}, stencilOp{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
			VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
		} load{};
		struct Store {
			VkAttachmentStoreOp op{VK_ATTACHMENT_STORE_OP_DONT_CARE}, stencilOp{VK_ATTACHMENT_STORE_OP_DONT_CARE};
			VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
		} store{};
	};

	struct SubpassAttachment {
		std::vector<VkAttachmentReference2> input_attachment_refs;
		std::vector<VkAttachmentReference2> color_attachment_refs;
		std::vector<VkAttachmentReference2> resolve_attachment_refs; // Empty or Same size as color_attachment_refs
		std::optional<VkAttachmentReference2> opt_depth_stencil_attachment_ref;
		std::vector<uint32_t> preserve_attachment_refs;
	};
	std::vector<VkAttachmentDescription2> attachments;
	mutable std::vector<VkSubpassDescription2> subpasses;
	std::vector<SubpassAttachment> subpass_attachments; // Same size as subpasses
	mutable std::vector<VkSubpassDependency2> dependencies;
	std::vector<VkMemoryBarrier2> dependency_barriers; // Same size as dependencies

	RenderPassState2 &SetAttachmentCount(uint32_t count) {
		attachments.resize(count, VkAttachmentDescription2{
		                              .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
		                          });
		return *this;
	}
	uint32_t GetAttachmentCount() const { return attachments.size(); }
	RenderPassState2 &SetAttachment(uint32_t id, const AttachmentInfo &info);
	RenderPassState2 &SetAttachment(uint32_t id, VkFormat format, const AttachmentInfo::Load &load,
	                                const AttachmentInfo::Store &store) {
		return SetAttachment(id, {.format = format, .samples = VK_SAMPLE_COUNT_1_BIT, .load = load, .store = store});
	}

	RenderPassState2 &SetSubpassCount(uint32_t count) {
		subpasses.resize(count, VkSubpassDescription2{
		                            .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
		                            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        });
		subpass_attachments.resize(count);
		return *this;
	}
	uint32_t GetSubpassCount() const { return subpasses.size(); }
	RenderPassState2 &SetSubpass(uint32_t id, const SubpassInfo &info);

	RenderPassState2 &SetDependencyCount(uint32_t count) {
		dependencies.resize(count, VkSubpassDependency2{
		                               .sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
		                           });
		dependency_barriers.resize(count, VkMemoryBarrier2{
		                                      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		                                  });
		return *this;
	}
	uint32_t GetDependencyCount() const { return dependencies.size(); }
	RenderPassState2 &SetDependency(uint32_t id, const DependencyInfo &info);
	RenderPassState2 &SetFramebufferDependency(uint32_t id, const DependencyInfo::Sync &src,
	                                           const DependencyInfo::Sync &dst) {
		return SetDependency(id, {.src = src, .dst = dst, .dependency_flags = VK_DEPENDENCY_BY_REGION_BIT});
	}
	RenderPassState2 &SetSrcExternalDependency(uint32_t id, const MemorySyncState &srcMemorySync,
	                                           const DependencyInfo::Sync &dst) {
		return SetDependency(
		    id, {.src = {.subpass = VK_SUBPASS_EXTERNAL, .sync = srcMemorySync}, .dst = dst, .dependency_flags = 0});
	}
	RenderPassState2 &SetDstExternalDependency(uint32_t id, const DependencyInfo::Sync &src,
	                                           const MemorySyncState &dstMemorySync) {
		return SetDependency(
		    id, {.src = src, .dst = {.subpass = VK_SUBPASS_EXTERNAL, .sync = dstMemorySync}, .dependency_flags = 0});
	}

	VkRenderPassCreateInfo2 GetRenderPassCreateInfo() const;
};

// TODO: Implement this
class RenderPassBuilder2 {};

} // namespace myvk

#endif

#include "myvk/RenderPass.hpp"

#include <algorithm>
#include <cassert>

namespace myvk {
Ptr<RenderPass> RenderPass::Create(const Ptr<Device> &device, const VkRenderPassCreateInfo &create_info) {
	auto ret = std::make_shared<RenderPass>();
	ret->m_device_ptr = device;

	if (vkCreateRenderPass(device->GetHandle(), &create_info, nullptr, &ret->m_render_pass) != VK_SUCCESS)
		return nullptr;
	return ret;
}
Ptr<RenderPass> RenderPass::Create(const Ptr<Device> &device, const VkRenderPassCreateInfo2 &create_info) {
	auto ret = std::make_shared<RenderPass>();
	ret->m_device_ptr = device;

	if (vkCreateRenderPass2(device->GetHandle(), &create_info, nullptr, &ret->m_render_pass) != VK_SUCCESS)
		return nullptr;
	return ret;
}
Ptr<RenderPass> RenderPass::Create(const Ptr<Device> &device, const RenderPassState &state) {
	VkRenderPassCreateInfo info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
	state.PopRenderPassCreateInfo(&info);
	return Create(device, info);
}
Ptr<RenderPass> RenderPass::Create(const Ptr<Device> &device, const RenderPassState2 &state) {
	return Create(device, state.GetRenderPassCreateInfo());
}

RenderPass::~RenderPass() {
	if (m_render_pass)
		vkDestroyRenderPass(m_device_ptr->GetHandle(), m_render_pass, nullptr);
}

uint32_t RenderPassState::get_subpass(const char *subpass_str) const {
	if (!subpass_str)
		return VK_SUBPASS_EXTERNAL;
	assert(m_subpass_map.find(subpass_str) != m_subpass_map.end());
	return m_subpass_map.at(subpass_str);
}
uint32_t RenderPassState::get_attachment(const char *attachment_str) const {
	if (!attachment_str)
		return VK_ATTACHMENT_UNUSED;
	assert(m_attachment_map.find(attachment_str) != m_attachment_map.end());
	return m_attachment_map.at(attachment_str);
}
void RenderPassState::insert_subpass_dependency(uint32_t src_subpass, uint32_t dst_subpass,
                                                VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
                                                VkAccessFlags src_access, VkAccessFlags dst_access,
                                                VkDependencyFlags dependency_flag) {
	SubpassDependencyKey key = {src_subpass, dst_subpass, dependency_flag};
	auto it = m_subpass_dependency_indices.find(key);
	if (it == m_subpass_dependency_indices.end()) {
		VkSubpassDependency dep = {src_subpass, dst_subpass, src_stage,      dst_stage,
		                           src_access,  dst_access,  dependency_flag};
		m_subpass_dependency_indices.insert({key, m_subpass_dependencies.size()});
		m_subpass_dependencies.push_back(dep);
	} else {
		VkSubpassDependency &dep = m_subpass_dependencies[it->second];
		dep.srcStageMask |= src_stage;
		dep.dstStageMask |= dst_stage;
		dep.srcAccessMask |= src_access;
		dep.dstAccessMask |= dst_access;
	}
}
void RenderPassState::Initialize(uint32_t subpass_count, uint32_t attachment_count) {
	m_subpass_infos.resize(subpass_count);
	m_subpass_descriptions.resize(subpass_count);
	for (uint32_t i = 0; i < subpass_count; ++i) {
		// used as flag for register
		m_subpass_descriptions[i].flags = VK_SUBPASS_DESCRIPTION_FLAG_BITS_MAX_ENUM;
		m_subpass_descriptions[i].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	}

	m_attachment_descriptions.resize(attachment_count);
	for (uint32_t i = 0; i < attachment_count; ++i) {
		// used as flag for register
		m_attachment_descriptions[i].flags = VK_ATTACHMENT_DESCRIPTION_FLAG_BITS_MAX_ENUM;
	}
}
RenderPassState::SubpassAttachmentHandle RenderPassState::RegisterSubpass(uint32_t subpass, const char *subpass_str,
                                                                          VkSubpassDescriptionFlags flags) {
	assert(subpass < get_subpass_count());
	assert(m_subpass_map.find(subpass_str) == m_subpass_map.end());
	assert(m_subpass_descriptions[subpass].flags == VK_SUBPASS_DESCRIPTION_FLAG_BITS_MAX_ENUM);

	m_subpass_map.insert({subpass_str, subpass});
	m_subpass_descriptions[subpass].flags = flags;
	return {this, subpass};
}
void RenderPassState::RegisterAttachment(uint32_t attachment, const char *attachment_str, VkFormat format,
                                         VkImageLayout initial_layout, VkImageLayout final_layout,
                                         VkSampleCountFlagBits samples, VkAttachmentLoadOp load_op,
                                         VkAttachmentStoreOp store_op, VkAttachmentLoadOp stencil_load_op,
                                         VkAttachmentStoreOp stencil_store_op) {
	assert(attachment < get_attachment_count());
	assert(m_attachment_map.find(attachment_str) == m_attachment_map.end());
	assert(m_attachment_descriptions[attachment].flags == VK_ATTACHMENT_DESCRIPTION_FLAG_BITS_MAX_ENUM);

	m_attachment_map.insert({attachment_str, attachment});
	VkAttachmentDescription &des = m_attachment_descriptions[attachment];
	des.flags = 0; // TODO: attachment description flags ?
	des.format = format;
	des.initialLayout = initial_layout;
	des.finalLayout = final_layout;
	des.samples = samples;
	des.loadOp = load_op;
	des.storeOp = store_op;
	des.stencilLoadOp = stencil_load_op;
	des.stencilStoreOp = stencil_store_op;
}

void RenderPassState::PopRenderPassCreateInfo(VkRenderPassCreateInfo *info) const {
	info->attachmentCount = m_attachment_descriptions.size();
	info->pAttachments = info->attachmentCount ? m_attachment_descriptions.data() : nullptr;

	info->subpassCount = m_subpass_descriptions.size();
	info->pSubpasses = info->subpassCount ? m_subpass_descriptions.data() : nullptr;

	info->dependencyCount = m_subpass_dependencies.size();
	info->pDependencies = info->dependencyCount ? m_subpass_dependencies.data() : nullptr;
}
void RenderPassState::AddExtraSubpassDependency(const char *src_subpass_str, const char *dst_subpass_str,
                                                VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
                                                VkAccessFlags src_access, VkAccessFlags dst_access,
                                                VkDependencyFlags dependency_flag) {
	insert_subpass_dependency(get_subpass(src_subpass_str), get_subpass(dst_subpass_str), src_stage, dst_stage,
	                          src_access, dst_access, dependency_flag);
}

RenderPassState::SubpassAttachmentHandle RenderPassState::SubpassAttachmentHandle::AddColorResolveAttachment(
    const char *attachment_str, VkImageLayout layout, const char *resolve_attachment_str, VkImageLayout resolve_layout,
    const char *generator_subpass_str, VkPipelineStageFlags generator_stage, VkPipelineStageFlags use_stage,
    VkAccessFlags generator_access, VkAccessFlags use_access, VkDependencyFlags dependency_flag) {
	auto &info = m_state_ptr->m_subpass_infos[m_subpass];
	info.color_attachment_references.push_back({m_state_ptr->get_attachment(attachment_str), layout});
	info.resolve_attachment_references.push_back({m_state_ptr->get_attachment(resolve_attachment_str), resolve_layout});

	auto &des = m_state_ptr->m_subpass_descriptions[m_subpass];
	des.pColorAttachments = info.color_attachment_references.data();
	des.colorAttachmentCount = info.color_attachment_references.size();
	des.pResolveAttachments = info.resolve_attachment_references.data();

	m_state_ptr->insert_subpass_dependency(m_state_ptr->get_subpass(generator_subpass_str), m_subpass, generator_stage,
	                                       use_stage, generator_access, use_access, dependency_flag);

	return *this;
}
RenderPassState::SubpassAttachmentHandle RenderPassState::SubpassAttachmentHandle::AddInputAttachment(
    const char *attachment_str, VkImageLayout layout, const char *generator_subpass_str,
    VkPipelineStageFlags generator_stage, VkPipelineStageFlags use_stage, VkAccessFlags generator_access,
    VkAccessFlags use_access, VkDependencyFlags dependency_flag) {
	auto &info = m_state_ptr->m_subpass_infos[m_subpass];
	info.input_attachment_references.push_back({m_state_ptr->get_attachment(attachment_str), layout});

	auto &des = m_state_ptr->m_subpass_descriptions[m_subpass];
	des.pInputAttachments = info.input_attachment_references.data();
	des.inputAttachmentCount = info.input_attachment_references.size();

	m_state_ptr->insert_subpass_dependency(m_state_ptr->get_subpass(generator_subpass_str), m_subpass, generator_stage,
	                                       use_stage, generator_access, use_access, dependency_flag);
	return *this;
}
/* RenderPassState::SubpassAttachmentHandle RenderPassState::SubpassAttachmentHandle::AddPreserveAttachment(
    const char *attachment_str, const char *generator_subpass_str, VkPipelineStageFlags generator_stage,
    VkPipelineStageFlags use_stage, VkAccessFlags generator_access, VkAccessFlags use_access,
    VkDependencyFlags dependency_flag) {
    auto &info = m_state_ptr->m_subpass_infos[m_subpass];
    info.preserve_attachments.push_back(m_state_ptr->get_attachment(attachment_str));

    auto &des = m_state_ptr->m_subpass_descriptions[m_subpass];
    des.pPreserveAttachments = info.preserve_attachments.data();
    des.preserveAttachmentCount = info.preserve_attachments.size();

    m_state_ptr->insert_subpass_dependency(m_state_ptr->get_subpass(generator_subpass_str), m_subpass, generator_stage,
                                           use_stage, generator_access, use_access, dependency_flag);
    return *this;
} */
RenderPassState::SubpassAttachmentHandle RenderPassState::SubpassAttachmentHandle::SetDepthStencilAttachment(
    const char *attachment_str, VkImageLayout layout, const char *src_subpass_str, VkPipelineStageFlags src_stage,
    VkPipelineStageFlags use_stage, VkAccessFlags src_access, VkAccessFlags use_access,
    VkDependencyFlags dependency_flag) {
	auto &info = m_state_ptr->m_subpass_infos[m_subpass];
	auto &des = m_state_ptr->m_subpass_descriptions[m_subpass];
	assert(des.pDepthStencilAttachment == nullptr);

	info.depth_stencil_attachment_reference = {m_state_ptr->get_attachment(attachment_str), layout};
	des.pDepthStencilAttachment = &(info.depth_stencil_attachment_reference);

	m_state_ptr->insert_subpass_dependency(m_state_ptr->get_subpass(src_subpass_str), m_subpass, src_stage, use_stage,
	                                       src_access, use_access, dependency_flag);
	return *this;
}

RenderPassState2 &RenderPassState2::SetAttachment(uint32_t id, const AttachmentInfo &info) {
	auto &attachment = attachments[id];
	attachment.format = info.format;
	attachment.samples = info.samples;
	attachment.loadOp = info.load.op;
	attachment.stencilLoadOp = info.load.stencilOp;
	attachment.initialLayout = info.load.layout;
	attachment.storeOp = info.store.op;
	attachment.stencilStoreOp = info.store.stencilOp;
	attachment.finalLayout = info.store.layout;
	return *this;
}
RenderPassState2 &RenderPassState2::SetSubpass(uint32_t id, const SubpassInfo &info) {
	auto &subpass = subpasses[id];
	auto &attachment = subpass_attachments[id];
	subpass.pipelineBindPoint = info.pipeline_bind_point;
	attachment.input_attachment_refs = [&info] {
		std::vector<VkAttachmentReference2> refs;
		refs.reserve(info.input_attachment_refs.size());
		for (auto &ref : info.input_attachment_refs) {
			refs.push_back({
			    .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
			    .attachment = ref.attachment,
			    .layout = ref.layout,
			    .aspectMask = ref.aspect_mask,
			});
		}
		return refs;
	}();
	attachment.color_attachment_refs = [&info] {
		std::vector<VkAttachmentReference2> refs;
		refs.reserve(info.color_attachment_refs.size());
		for (auto &ref : info.color_attachment_refs) {
			refs.push_back({
			    .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
			    .attachment = ref.attachment,
			    .layout = ref.layout,
			});
		}
		return refs;
	}();
	attachment.resolve_attachment_refs = [&info] {
		std::vector<VkAttachmentReference2> refs;
		if (std::any_of(info.color_attachment_refs.begin(), info.color_attachment_refs.end(),
		                [](const auto &ref) { return ref.resolveAttachment != VK_ATTACHMENT_UNUSED; })) {
			refs.reserve(info.color_attachment_refs.size());
			for (auto &ref : info.color_attachment_refs) {
				refs.push_back({
				    .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
				    .attachment = ref.resolveAttachment,
				    .layout = ref.resolveLayout,
				});
			}
		}
		return refs;
	}();
	attachment.opt_depth_stencil_attachment_ref = [&info]() -> std::optional<VkAttachmentReference2> {
		const auto &opt_ref = info.opt_depth_stencil_attachment_ref;
		if (opt_ref.has_value())
			return VkAttachmentReference2{
			    .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
			    .attachment = opt_ref->attachment,
			    .layout = opt_ref->layout,
			};
		return std::nullopt;
	}();
	attachment.preserve_attachment_refs = info.preserve_attachment_refs;
	return *this;
}
RenderPassState2 &RenderPassState2::SetDependency(uint32_t id, const DependencyInfo &info) {
	auto &dependency = dependencies[id];
	auto &barrier = dependency_barriers[id];
	dependency.srcSubpass = info.src.subpass;
	barrier.srcStageMask = info.src.sync.stage_mask;
	barrier.srcAccessMask = info.src.sync.access_mask;
	dependency.dstSubpass = info.dst.subpass;
	barrier.dstStageMask = info.dst.sync.stage_mask;
	barrier.dstAccessMask = info.dst.sync.access_mask;
	dependency.dependencyFlags = info.dependency_flags;
	return *this;
}
VkRenderPassCreateInfo2 RenderPassState2::GetRenderPassCreateInfo() const {
	assert(dependency_barriers.size() == GetDependencyCount());
	assert(subpass_attachments.size() == GetSubpassCount());

	for (uint32_t subpass_id = 0; subpass_id < GetSubpassCount(); ++subpass_id) {
		auto &subpass = subpasses[subpass_id];
		const auto &subpass_att = subpass_attachments[subpass_id];

		// Input Attachments
		subpass.inputAttachmentCount = subpass_att.input_attachment_refs.size();
		subpass.pInputAttachments = subpass_att.input_attachment_refs.data();

		// Color (+ Resolve) Attachments
		subpass.colorAttachmentCount = subpass_att.color_attachment_refs.size();
		subpass.pColorAttachments = subpass_att.color_attachment_refs.data();
		subpass.pResolveAttachments =
		    subpass_att.resolve_attachment_refs.empty() ? nullptr : subpass_att.resolve_attachment_refs.data();

		if (subpass.pResolveAttachments) {
			assert(subpass_att.resolve_attachment_refs.size() == subpass.colorAttachmentCount);
		}

		// Depth-Stencil Attachment
		subpass.pDepthStencilAttachment = subpass_att.opt_depth_stencil_attachment_ref.has_value()
		                                      ? &subpass_att.opt_depth_stencil_attachment_ref.value()
		                                      : nullptr;

		// Preserve Attachments
		subpass.preserveAttachmentCount = subpass_att.preserve_attachment_refs.size();
		subpass.pPreserveAttachments = subpass_att.preserve_attachment_refs.data();
	}

	for (uint32_t dependency_id = 0; dependency_id < GetDependencyCount(); ++dependency_id) {
		// According to https://registry.khronos.org/VulkanSC/specs/1.0-extensions/man/html/VkSubpassDependency2.html
		// pNext of VkSubpassDependency2 can only be VkMemoryBarrier2
		dependencies[dependency_id].pNext = dependency_barriers.data() + dependency_id;
	}

	return VkRenderPassCreateInfo2{
	    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
	    .attachmentCount = GetAttachmentCount(),
	    .pAttachments = attachments.data(),
	    .subpassCount = GetSubpassCount(),
	    .pSubpasses = subpasses.data(),
	    .dependencyCount = GetDependencyCount(),
	    .pDependencies = dependencies.data(),
	};
}

} // namespace myvk

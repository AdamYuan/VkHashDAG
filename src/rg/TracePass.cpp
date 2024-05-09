//
// Created by adamyuan on 5/7/24.
//

#include "TracePass.hpp"

namespace rg {

namespace tracer_pass {
struct PC_Data {
	glm::vec3 pos, look, side, up;
	uint32_t width, height;
	uint32_t voxel_level;
	uint32_t dag_root, dag_leaf_level;
	uint32_t color_root, color_leaf_level;
	float proj_factor;
	uint32_t type;
	uint32_t beam_opt;
};
} // namespace tracer_pass

TracePass::TracePass(myvk_rg::Parent parent, const Args &args) : myvk_rg::GraphicsPassBase(parent) {
	m_camera_ptr = args.camera;
	m_node_pool_ptr = args.node_pool;
	m_color_pool_ptr = args.color_pool;

	const auto &device = GetRenderGraphPtr()->GetDevicePtr();
	VkSamplerReductionModeCreateInfo reduction_create_info = {
	    .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
	    .reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN,
	};
	VkSamplerCreateInfo sampler_create_info = {
	    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	    .pNext = &reduction_create_info,
	    .magFilter = VK_FILTER_LINEAR,
	    .minFilter = VK_FILTER_LINEAR,
	    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
	    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	    .minLod = 0.0f,
	    .maxLod = VK_LOD_CLAMP_NONE,
	};

	AddColorAttachmentInput<myvk_rg::Usage::kColorAttachmentW>(0, {"image"}, args.image);
	AddDescriptorInput<myvk_rg::Usage::kStorageBufferR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({0}, {"dag_nodes"},
	                                                                                             args.dag_nodes);
	AddDescriptorInput<myvk_rg::Usage::kStorageBufferR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({1}, {"color_nodes"},
	                                                                                             args.color_nodes);
	AddDescriptorInput<myvk_rg::Usage::kStorageBufferR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({2}, {"color_leaves"},
	                                                                                             args.color_leaves);
	AddDescriptorInput<myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
	    {3}, {"beam"}, args.beam, myvk::Sampler::Create(device, sampler_create_info));
}

myvk::Ptr<myvk::GraphicsPipeline> TracePass::CreatePipeline() const {
	const auto &device = GetRenderGraphPtr()->GetDevicePtr();

	auto pipeline_layout = myvk::PipelineLayout::Create(device, {GetVkDescriptorSetLayout()},
	                                                    {VkPushConstantRange{
	                                                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	                                                        .offset = 0,
	                                                        .size = sizeof(tracer_pass::PC_Data),
	                                                    }});

	constexpr uint32_t kVertSpv[] = {
#include <shader/quad.vert.u32>

	};
	constexpr uint32_t kFragSpv[] = {
#include <shader/dag.frag.u32>

	};

	std::shared_ptr<myvk::ShaderModule> vert_shader_module, frag_shader_module;
	vert_shader_module = myvk::ShaderModule::Create(device, kVertSpv, sizeof(kVertSpv));
	frag_shader_module = myvk::ShaderModule::Create(device, kFragSpv, sizeof(kFragSpv));

	std::vector shader_stages = {vert_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
	                             frag_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)};

	myvk::GraphicsPipelineState pipeline_state = {};
	auto extent = GetRenderGraphPtr()->GetCanvasSize();
	pipeline_state.m_viewport_state.Enable(std::vector<VkViewport>{{0, 0, (float)extent.width, (float)extent.height}},
	                                       std::vector<VkRect2D>{{{0, 0}, extent}});
	pipeline_state.m_vertex_input_state.Enable();
	pipeline_state.m_input_assembly_state.Enable(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipeline_state.m_rasterization_state.Initialize(VK_POLYGON_MODE_FILL, VK_FRONT_FACE_COUNTER_CLOCKWISE,
	                                                VK_CULL_MODE_FRONT_BIT);
	pipeline_state.m_multisample_state.Enable(VK_SAMPLE_COUNT_1_BIT);
	pipeline_state.m_color_blend_state.Enable(1, VK_FALSE);

	return myvk::GraphicsPipeline::Create(pipeline_layout, GetVkRenderPass(), shader_stages, pipeline_state,
	                                      GetSubpass());
}

void TracePass::CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const {
	command_buffer->CmdBindPipeline(GetVkPipeline());
	command_buffer->CmdBindDescriptorSets({GetVkDescriptorSet()}, GetVkPipeline());

	auto extent = GetRenderGraphPtr()->GetCanvasSize();
	float aspect_ratio = float(extent.width) / float(extent.height);

	auto look_side_up = m_camera_ptr->GetLookSideUp(aspect_ratio);

	float inv_2tan_half_fov = 1.0f / (2.0f * glm::tan(0.5f * m_camera_ptr->m_fov));
	float screen_divisor = 1.0f;
	float screen_tolerance = 1.0f / (float(extent.height) / screen_divisor);
	float projection_factor = inv_2tan_half_fov / screen_tolerance;

	tracer_pass::PC_Data pc_data{
	    .pos = m_camera_ptr->m_position,
	    .look = look_side_up.look,
	    .side = look_side_up.side,
	    .up = look_side_up.up,
	    .width = extent.width,
	    .height = extent.height,
	    .voxel_level = m_node_pool_ptr->GetConfig().GetVoxelLevel(),
	    .dag_root = *m_node_pool_ptr->GetRoot(),
	    .dag_leaf_level = m_node_pool_ptr->GetConfig().GetLeafLevel(),
	    .color_root = m_color_pool_ptr->GetRoot().pointer,
	    .color_leaf_level = m_color_pool_ptr->GetLeafLevel(),
	    .proj_factor = projection_factor,
	    .type = m_render_type,
	    .beam_opt = m_beam_optimization,
	};

	command_buffer->CmdPushConstants(GetVkPipeline()->GetPipelineLayoutPtr(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
	                                 sizeof(pc_data), &pc_data);
	command_buffer->CmdDraw(3, 1, 0, 0);
}

} // namespace rg
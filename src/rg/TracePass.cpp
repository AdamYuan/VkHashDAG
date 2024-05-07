//
// Created by adamyuan on 5/7/24.
//

#include "TracePass.hpp"

namespace rg {

TracePass::TracePass(myvk_rg::Parent parent, const Args &args) : myvk_rg::GraphicsPassBase(parent) {
	m_camera_ptr = args.camera;
	m_node_pool_ptr = args.node_pool;
	m_color_pool_ptr = args.color_pool;

	AddColorAttachmentInput<myvk_rg::Usage::kColorAttachmentW>(0, {"image"}, args.image);
	AddDescriptorInput<myvk_rg::Usage::kStorageBufferR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({0}, {"dag_nodes"},
	                                                                                             args.dag_nodes);
	AddDescriptorInput<myvk_rg::Usage::kStorageBufferR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({1}, {"color_nodes"},
	                                                                                             args.color_nodes);
	AddDescriptorInput<myvk_rg::Usage::kStorageBufferR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({2}, {"color_leaves"},
	                                                                                             args.color_leaves);
}

myvk::Ptr<myvk::GraphicsPipeline> TracePass::CreatePipeline() const {
	const auto &device = GetRenderGraphPtr()->GetDevicePtr();

	auto pipeline_layout = myvk::PipelineLayout::Create(device, {GetVkDescriptorSetLayout()},
	                                                    {VkPushConstantRange{
	                                                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	                                                        .offset = 0,
	                                                        .size = 13 * sizeof(float) + 5 * sizeof(uint32_t),
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

	static_assert(sizeof(float) == sizeof(uint32_t));
	uint32_t pc_data[18];
	*(glm::vec3 *)(pc_data) = m_camera_ptr->m_position;
	*(Camera::LookSideUp *)(pc_data + 3) = m_camera_ptr->GetLookSideUp(aspect_ratio);
	pc_data[12] = extent.width;
	pc_data[13] = extent.height;
	pc_data[14] = *m_node_pool_ptr->GetRoot();
	pc_data[15] = m_node_pool_ptr->GetConfig().GetNodeLevels();
	float inv_2tan_half_fov = 1.0f / (2.0f * glm::tan(0.5f * m_camera_ptr->m_fov));
	float screen_divisor = 1.0f;
	float screen_tolerance = 1.0f / (float(extent.height) / screen_divisor);
	float projection_factor = inv_2tan_half_fov / screen_tolerance;
	*(float *)(pc_data + 16) = projection_factor;
	pc_data[17] = 0; // static_cast<uint32_t>(render_type);

	command_buffer->CmdPushConstants(GetVkPipeline()->GetPipelineLayoutPtr(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
	                                 sizeof(pc_data), pc_data);
	command_buffer->CmdDraw(3, 1, 0, 0);
}

} // namespace rg
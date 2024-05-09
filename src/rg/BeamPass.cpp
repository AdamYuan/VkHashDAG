//
// Created by adamyuan on 5/9/24.
//

#include "BeamPass.hpp"

namespace rg {

namespace beam_pass {

inline constexpr uint32_t kBlockSize = 8;
inline constexpr VkExtent2D GetBeamSize(VkExtent2D extent) {
	return {
	    .width = (extent.width + kBlockSize - 1u) / kBlockSize,
	    .height = (extent.height + kBlockSize - 1u) / kBlockSize,
	};
}

struct PC_Data {
	glm::vec3 pos, look, side, up;
	uint32_t width, height;
	uint32_t dag_root, dag_leaf_level;
	float proj_factor;
};

} // namespace beam_pass

BeamPass::BeamPass(myvk_rg::Parent parent, const Args &args) : myvk_rg::GraphicsPassBase(parent) {
	m_camera_ptr = args.camera;
	m_node_pool_ptr = args.node_pool;

	auto beam = CreateResource<myvk_rg::ManagedImage>({"beam"}, VK_FORMAT_R32_SFLOAT);
	beam->SetLoadOp(VK_ATTACHMENT_LOAD_OP_DONT_CARE);
	beam->SetSizeFunc([](const VkExtent2D &extent) { return myvk_rg::SubImageSize{beam_pass::GetBeamSize(extent)}; });

	AddColorAttachmentInput<myvk_rg::Usage::kColorAttachmentW>(0, {"beam"}, beam->Alias());
	AddDescriptorInput<myvk_rg::Usage::kStorageBufferR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({0}, {"dag_nodes"},
	                                                                                             args.dag_nodes);
}

myvk::Ptr<myvk::GraphicsPipeline> BeamPass::CreatePipeline() const {
	const auto &device = GetRenderGraphPtr()->GetDevicePtr();

	auto pipeline_layout = myvk::PipelineLayout::Create(device, {GetVkDescriptorSetLayout()},
	                                                    {VkPushConstantRange{
	                                                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	                                                        .offset = 0,
	                                                        .size = sizeof(beam_pass::PC_Data),
	                                                    }});

	constexpr uint32_t kVertSpv[] = {
#include <shader/quad.vert.u32>

	};
	constexpr uint32_t kFragSpv[] = {
#include <shader/beam.frag.u32>

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
void BeamPass::CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const {
	command_buffer->CmdBindPipeline(GetVkPipeline());
	command_buffer->CmdBindDescriptorSets({GetVkDescriptorSet()}, GetVkPipeline());

	auto extent = GetRenderGraphPtr()->GetCanvasSize();
	auto beam_extent = beam_pass::GetBeamSize(extent);
	float aspect_ratio = float(extent.width) / float(extent.height);

	auto look_side_up = m_camera_ptr->GetLookSideUp(aspect_ratio);

	float inv_2tan_half_fov = 1.0f / (2.0f * glm::tan(0.5f * m_camera_ptr->m_fov));
	float screen_divisor = beam_pass::kBlockSize;
	float screen_tolerance = 1.0f / (float(extent.height) / screen_divisor);
	float projection_factor = inv_2tan_half_fov / screen_tolerance;

	beam_pass::PC_Data pc_data{
	    .pos = m_camera_ptr->m_position,
	    .look = look_side_up.look,
	    .side = look_side_up.side,
	    .up = look_side_up.up,
	    .width = beam_extent.width,
	    .height = beam_extent.height,
	    .dag_root = *m_node_pool_ptr->GetRoot(),
	    .dag_leaf_level = m_node_pool_ptr->GetConfig().GetLeafLevel(),
	    .proj_factor = projection_factor,
	};

	command_buffer->CmdPushConstants(GetVkPipeline()->GetPipelineLayoutPtr(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
	                                 sizeof(pc_data), &pc_data);
	command_buffer->CmdDraw(3, 1, 0, 0);
}

} // namespace rg

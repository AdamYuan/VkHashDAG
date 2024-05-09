//
// Created by adamyuan on 5/9/24.
//

#include "CrosshairPass.hpp"

namespace rg {

CrosshairPass::CrosshairPass(myvk_rg::Parent parent, const myvk_rg::Image &image) : myvk_rg::GraphicsPassBase(parent) {
	AddColorAttachmentInput<myvk_rg::Usage::kColorAttachmentRW>(0, {"image"}, image);
}

myvk::Ptr<myvk::GraphicsPipeline> CrosshairPass::CreatePipeline() const {
	const auto &device = GetRenderGraphPtr()->GetDevicePtr();

	auto pipeline_layout = myvk::PipelineLayout::Create(device, {},
	                                                    {VkPushConstantRange{
	                                                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	                                                        .offset = 0,
	                                                        .size = sizeof(VkExtent2D),
	                                                    }});

	constexpr uint32_t kVertSpv[] = {
#include <shader/crosshair.vert.u32>

	};
	constexpr uint32_t kFragSpv[] = {
#include <shader/crosshair.frag.u32>

	};

	auto vert_shader_module = myvk::ShaderModule::Create(device, kVertSpv, sizeof(kVertSpv));
	auto frag_shader_module = myvk::ShaderModule::Create(device, kFragSpv, sizeof(kFragSpv));

	std::vector shader_stages = {vert_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
	                             frag_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)};

	myvk::GraphicsPipelineState pipeline_state = {};
	auto extent = GetRenderGraphPtr()->GetCanvasSize();
	pipeline_state.m_viewport_state.Enable(std::vector<VkViewport>{{0, 0, (float)extent.width, (float)extent.height}},
	                                       std::vector<VkRect2D>{{{0, 0}, extent}});
	pipeline_state.m_vertex_input_state.Enable();
	pipeline_state.m_input_assembly_state.Enable(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
	pipeline_state.m_rasterization_state.Initialize(VK_POLYGON_MODE_FILL, VK_FRONT_FACE_COUNTER_CLOCKWISE,
	                                                VK_CULL_MODE_FRONT_BIT);
	pipeline_state.m_rasterization_state.m_create_info.lineWidth = 4.0f;
	pipeline_state.m_multisample_state.Enable(VK_SAMPLE_COUNT_1_BIT);
	pipeline_state.m_color_blend_state.Enable(1, VK_FALSE);
	pipeline_state.m_color_blend_state.m_create_info.logicOpEnable = VK_TRUE;
	pipeline_state.m_color_blend_state.m_create_info.logicOp = VK_LOGIC_OP_INVERT;

	return myvk::GraphicsPipeline::Create(pipeline_layout, GetVkRenderPass(), shader_stages, pipeline_state,
	                                      GetSubpass());
}

void CrosshairPass::CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const {
	command_buffer->CmdBindPipeline(GetVkPipeline());
	auto extent = GetRenderGraphPtr()->GetCanvasSize();
	command_buffer->CmdPushConstants(GetVkPipeline()->GetPipelineLayoutPtr(), VK_SHADER_STAGE_VERTEX_BIT, 0,
	                                 sizeof(extent), &extent);
	command_buffer->CmdDraw(4, 1, 0, 0);
}

} // namespace rg
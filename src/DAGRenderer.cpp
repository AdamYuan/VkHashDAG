//
// Created by adamyuan on 1/16/24.
//

#include "DAGRenderer.hpp"

#include <myvk/ShaderModule.hpp>

void DAGRenderer::create_pipeline(const myvk::Ptr<myvk::RenderPass> &render_pass, uint32_t subpass) {
	const auto &device = render_pass->GetDevicePtr();

	auto pipeline_layout = myvk::PipelineLayout::Create(device, {m_dag_node_pool_ptr->GetDescriptorSetLayout()},
	                                                    {VkPushConstantRange{
	                                                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	                                                        .offset = 0,
	                                                        .size = 13 * sizeof(float) + 5 * sizeof(uint32_t),
	                                                    }});

	constexpr uint32_t kQuadVertSpv[] = {
#include <shader/quad.vert.u32>
	};
	constexpr uint32_t kDAGFragSpv[] = {
#include <shader/dag.frag.u32>
	};

	std::shared_ptr<myvk::ShaderModule> vert_shader_module, frag_shader_module;
	vert_shader_module = myvk::ShaderModule::Create(device, kQuadVertSpv, sizeof(kQuadVertSpv));
	frag_shader_module = myvk::ShaderModule::Create(device, kDAGFragSpv, sizeof(kDAGFragSpv));

	std::vector<VkPipelineShaderStageCreateInfo> shader_stages = {
	    vert_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
	    frag_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)};

	myvk::GraphicsPipelineState pipeline_state = {};
	/* auto extent = GetRenderGraphPtr()->GetCanvasSize();
	pipeline_state.m_viewport_state.Enable(std::vector<VkViewport>{{0, 0, (float)extent.width, (float)extent.height}},
	                                       std::vector<VkRect2D>{{{0, 0}, extent}}); */
	pipeline_state.m_viewport_state.Enable(1, 1);
	pipeline_state.m_dynamic_state.Enable({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});
	pipeline_state.m_vertex_input_state.Enable();
	pipeline_state.m_input_assembly_state.Enable(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipeline_state.m_rasterization_state.Initialize(VK_POLYGON_MODE_FILL, VK_FRONT_FACE_COUNTER_CLOCKWISE,
	                                                VK_CULL_MODE_FRONT_BIT);
	pipeline_state.m_multisample_state.Enable(VK_SAMPLE_COUNT_1_BIT);
	pipeline_state.m_color_blend_state.Enable(1, VK_FALSE);

	m_pipeline = myvk::GraphicsPipeline::Create(pipeline_layout, render_pass, shader_stages, pipeline_state, subpass);
}

void DAGRenderer::CmdDrawPipeline(const myvk::Ptr<myvk::CommandBuffer> &command_buffer, const Camera &camera,
                                  uint32_t width, uint32_t height, DAGRenderType render_type) const {
	float aspect_ratio = float(width) / float(height);
	command_buffer->CmdBindPipeline(m_pipeline);
	command_buffer->CmdBindDescriptorSets({m_dag_node_pool_ptr->GetDescriptorSet()}, m_pipeline, {});
	command_buffer->CmdSetViewport({{0, 0, float(width), float(height)}});
	command_buffer->CmdSetScissor({{{0, 0}, {width, height}}});

	static_assert(sizeof(float) == sizeof(uint32_t));
	uint32_t pc_data[18];
	*(glm::vec3 *)(pc_data) = camera.m_position;
	*(Camera::LookSideUp *)(pc_data + 3) = camera.GetLookSideUp(aspect_ratio);
	pc_data[12] = width;
	pc_data[13] = height;
	pc_data[14] = *m_dag_node_pool_ptr->GetRoot();
	pc_data[15] = m_dag_node_pool_ptr->GetConfig().GetNodeLevels();
	float inv_2tan_half_fov = 1.0f / (2.0f * glm::tan(0.5f * camera.m_fov));
	float screen_divisor = 1.0f;
	float screen_tolerance = 1.0f / (float(height) / screen_divisor);
	float projection_factor = inv_2tan_half_fov / screen_tolerance;
	*(float *)(pc_data + 16) = projection_factor;
	pc_data[17] = static_cast<uint32_t>(render_type);

	command_buffer->CmdPushConstants(m_pipeline->GetPipelineLayoutPtr(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
	                                 sizeof(pc_data), pc_data);
	command_buffer->CmdDraw(3, 1, 0, 0);
}

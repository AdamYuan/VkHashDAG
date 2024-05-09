//
// Created by adamyuan on 5/7/24.
//

#include "DAGRenderGraph.hpp"

#include "BeamPass.hpp"
#include "TracePass.hpp"
#include <myvk_rg/pass/ImGuiPass.hpp>
#include <myvk_rg/resource/InputBuffer.hpp>
#include <myvk_rg/resource/SwapchainImage.hpp>

namespace rg {

DAGRenderGraph::DAGRenderGraph(const myvk::Ptr<myvk::FrameManager> &frame_manager, const myvk::Ptr<Camera> &camera,
                               const myvk::Ptr<DAGNodePool> &node_pool, const myvk::Ptr<DAGColorPool> &color_pool,
                               bool beam_opt)
    : RenderGraphBase(frame_manager->GetDevicePtr()), m_frame_manager_ptr(frame_manager), m_camera_ptr(camera),
      m_node_pool_ptr(node_pool), m_color_pool_ptr(color_pool), m_beam_opt{beam_opt} {
	make_resources();
	make_passes();
}

void DAGRenderGraph::make_resources() {
	ClearResources();
	auto dag_nodes = CreateResource<myvk_rg::InputBuffer>({"dag_nodes"}, m_node_pool_ptr->GetBuffer());
	dag_nodes->SetSyncType(myvk_rg::ExternalSyncType::kCustom);
	auto color_nodes = CreateResource<myvk_rg::InputBuffer>({"color_nodes"}, m_color_pool_ptr->GetNodeBuffer());
	color_nodes->SetSyncType(myvk_rg::ExternalSyncType::kCustom);
	auto color_leaves = CreateResource<myvk_rg::InputBuffer>({"color_leaves"}, m_color_pool_ptr->GetLeafBuffer());
	color_leaves->SetSyncType(myvk_rg::ExternalSyncType::kCustom);

	auto swapchain_image = CreateResource<myvk_rg::SwapchainImage>({"swapchain_image"}, m_frame_manager_ptr);
	swapchain_image->SetLoadOp(VK_ATTACHMENT_LOAD_OP_DONT_CARE);
}
void DAGRenderGraph::make_passes() {
	ClearPasses();
	ClearResults();

	auto dag_nodes = GetResource<myvk_rg::InputBuffer>({"dag_nodes"});
	auto color_nodes = GetResource<myvk_rg::InputBuffer>({"color_nodes"});
	auto color_leaves = GetResource<myvk_rg::InputBuffer>({"color_leaves"});
	auto swapchain_image = GetResource<myvk_rg::SwapchainImage>({"swapchain_image"});

	if (m_beam_opt) {
		CreatePass<BeamPass>({"beam_pass"}, BeamPass::Args{
		                                        .dag_nodes = dag_nodes->Alias(),
		                                        .camera = m_camera_ptr,
		                                        .node_pool = m_node_pool_ptr,
		                                    });
	}

	auto trace_pass = CreatePass<TracePass>(
	    {"trace_pass"}, TracePass::Args{
	                        .image = swapchain_image->Alias(),
	                        .opt_beam = m_beam_opt ? GetPass<BeamPass>({"beam_pass"})->GetBeamOutput()
	                                               : std::optional<myvk_rg::Image>{std::nullopt},
	                        .dag_nodes = dag_nodes->Alias(),
	                        .color_nodes = color_nodes->Alias(),
	                        .color_leaves = color_leaves->Alias(),
	                        .camera = m_camera_ptr,
	                        .node_pool = m_node_pool_ptr,
	                        .color_pool = m_color_pool_ptr,
	                    });
	auto imgui_pass = CreatePass<myvk_rg::ImGuiPass>({"imgui_pass"}, trace_pass->GetImageOutput());
	AddResult({"out"}, imgui_pass->GetImageOutput());
}

void DAGRenderGraph::PreExecute() const {}

void DAGRenderGraph::SetRenderType(uint32_t x) { GetPass<TracePass>({"trace_pass"})->SetRenderType(x); }
void DAGRenderGraph::SetBeamOpt(bool b) {
	if (b == m_beam_opt)
		return;
	m_beam_opt = b;
	make_passes();
}

} // namespace rg

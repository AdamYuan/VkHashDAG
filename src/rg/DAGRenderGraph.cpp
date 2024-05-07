//
// Created by adamyuan on 5/7/24.
//

#include "DAGRenderGraph.hpp"

#include "TracePass.hpp"
#include <myvk_rg/pass/ImGuiPass.hpp>
#include <myvk_rg/resource/InputBuffer.hpp>
#include <myvk_rg/resource/SwapchainImage.hpp>

namespace rg {

DAGRenderGraph::DAGRenderGraph(const myvk::Ptr<myvk::FrameManager> &frame_manager, const myvk::Ptr<Camera> &camera,
                               const myvk::Ptr<DAGNodePool> &node_pool, const myvk::Ptr<DAGColorPool> &color_pool)
    : RenderGraphBase(frame_manager->GetDevicePtr()) {

	auto dag_nodes = CreateResource<myvk_rg::InputBuffer>({"dag_nodes"}, node_pool->GetBuffer());
	dag_nodes->SetSyncType(myvk_rg::ExternalSyncType::kCustom);
	auto color_nodes = CreateResource<myvk_rg::InputBuffer>({"color_nodes"}, color_pool->GetNodeBuffer());
	color_nodes->SetSyncType(myvk_rg::ExternalSyncType::kCustom);
	auto color_leaves = CreateResource<myvk_rg::InputBuffer>({"color_leaves"}, color_pool->GetLeafBuffer());
	color_leaves->SetSyncType(myvk_rg::ExternalSyncType::kCustom);

	auto swapchain_image = CreateResource<myvk_rg::SwapchainImage>({"swapchain_image"}, frame_manager);
	swapchain_image->SetLoadOp(VK_ATTACHMENT_LOAD_OP_DONT_CARE);

	auto trace_pass = CreatePass<TracePass>({"trace_pass"}, TracePass::Args{
	                                                            .image = swapchain_image->Alias(),
	                                                            .dag_nodes = dag_nodes->Alias(),
	                                                            .color_nodes = color_nodes->Alias(),
	                                                            .color_leaves = color_leaves->Alias(),
	                                                            .camera = camera,
	                                                            .node_pool = node_pool,
	                                                            .color_pool = color_pool,
	                                                        });
	auto imgui_pass = CreatePass<myvk_rg::ImGuiPass>({"imgui_pass"}, trace_pass->GetImageOutput());
	AddResult({"out"}, imgui_pass->GetImageOutput());
}

void DAGRenderGraph::PreExecute() const {}

void DAGRenderGraph::SetRenderType(uint32_t x) { GetPass<TracePass>({"trace_pass"})->SetRenderType(x); }

} // namespace rg

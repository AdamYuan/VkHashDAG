//
// Created by adamyuan on 5/7/24.
//

#pragma once
#ifndef RG_DAGRENDERGRAPH_HPP
#define RG_DAGRENDERGRAPH_HPP

#include "../Camera.hpp"
#include "../DAGColorPool.hpp"
#include "../DAGNodePool.hpp"

#include <myvk_rg/RenderGraph.hpp>

namespace rg {

class DAGRenderGraph final : public myvk_rg::RenderGraphBase {
public:
	explicit DAGRenderGraph(const myvk::Ptr<myvk::FrameManager> &frame_manager, const myvk::Ptr<Camera> &camera,
	                        const myvk::Ptr<DAGNodePool> &node_pool, const myvk::Ptr<DAGColorPool> &color_pool);
	~DAGRenderGraph() override = default;
	void PreExecute() const override;

	void SetRenderType(uint32_t x);
	void SetBeamOptimization(bool b);
};

} // namespace rg

#endif

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
private:
	myvk::Ptr<myvk::FrameManager> m_frame_manager_ptr;
	myvk::Ptr<Camera> m_camera_ptr;
	myvk::Ptr<DAGNodePool> m_node_pool_ptr;
	myvk::Ptr<DAGColorPool> m_color_pool_ptr;

	bool m_beam_opt{false};

	void make_resources();
	void make_passes();

public:
	explicit DAGRenderGraph(const myvk::Ptr<myvk::FrameManager> &frame_manager, const myvk::Ptr<Camera> &camera,
	                        const myvk::Ptr<DAGNodePool> &node_pool, const myvk::Ptr<DAGColorPool> &color_pool,
	                        bool beam_opt);
	~DAGRenderGraph() override = default;
	void PreExecute() const override;

	void SetRenderType(uint32_t x);
	void SetBeamOpt(bool b);
};

} // namespace rg

#endif

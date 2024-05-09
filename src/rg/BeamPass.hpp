//
// Created by adamyuan on 5/9/24.
//

#pragma once
#ifndef VKHASHDAG_RG_BEAMPASS_HPP
#define VKHASHDAG_RG_BEAMPASS_HPP

#include "../Camera.hpp"
#include "../DAGNodePool.hpp"

#include <myvk_rg/RenderGraph.hpp>

namespace rg {

class BeamPass final : public myvk_rg::GraphicsPassBase {
	myvk::Ptr<Camera> m_camera_ptr;
	myvk::Ptr<DAGNodePool> m_node_pool_ptr;
	uint32_t m_render_type{};

public:
	struct Args {
		const myvk_rg::Buffer &dag_nodes;
		const myvk::Ptr<Camera> &camera;
		const myvk::Ptr<DAGNodePool> &node_pool;
	};
	BeamPass(myvk_rg::Parent parent, const Args &args);
	inline ~BeamPass() override = default;

	myvk::Ptr<myvk::GraphicsPipeline> CreatePipeline() const override;
	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const override;

	inline auto GetBeamOutput() const { return MakeImageOutput({"beam"}); }
};

} // namespace rg

#endif

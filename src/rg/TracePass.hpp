//
// Created by adamyuan on 5/7/24.
//

#pragma once
#ifndef RG_TRACEPASS_HPP
#define RG_TRACEPASS_HPP

#include "../Camera.hpp"
#include "../DAGColorPool.hpp"
#include "../DAGNodePool.hpp"

#include <myvk_rg/RenderGraph.hpp>

namespace rg {

class TracePass final : public myvk_rg::GraphicsPassBase {
	myvk::Ptr<Camera> m_camera_ptr;
	myvk::Ptr<DAGNodePool> m_node_pool_ptr;
	myvk::Ptr<DAGColorPool> m_color_pool_ptr;
	uint32_t m_render_type{};

public:
	struct Args {
		const myvk_rg::Image &image, &beam;
		const myvk_rg::Buffer &dag_nodes, &color_nodes, &color_leaves;
		const myvk::Ptr<Camera> &camera;
		const myvk::Ptr<DAGNodePool> &node_pool;
		const myvk::Ptr<DAGColorPool> &color_pool;
	};
	TracePass(myvk_rg::Parent parent, const Args &args);
	inline ~TracePass() override = default;

	myvk::Ptr<myvk::GraphicsPipeline> CreatePipeline() const override;
	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const override;

	void SetRenderType(uint32_t x) { m_render_type = x; }

	inline auto GetImageOutput() const { return MakeImageOutput({"image"}); }
};

} // namespace rg

#endif

//
// Created by adamyuan on 1/16/24.
//

#pragma once
#ifndef VKHASHDAG_DAGRENDERER_HPP
#define VKHASHDAG_DAGRENDERER_HPP

#include <myvk/CommandBuffer.hpp>
#include <myvk/GraphicsPipeline.hpp>
#include <myvk/RenderPass.hpp>

#include "Camera.hpp"
#include "DAGNodePool.hpp"

enum class DAGRenderType { kDiffuse, kNormal, kIteration };

class DAGRenderer {
private:
	myvk::Ptr<DAGNodePool> m_dag_node_pool_ptr;
	myvk::Ptr<myvk::GraphicsPipeline> m_pipeline;

	void create_pipeline(const myvk::Ptr<myvk::RenderPass> &render_pass, uint32_t subpass);

public:
	inline DAGRenderer(const myvk::Ptr<DAGNodePool> &dag_node_pool_ptr, const myvk::Ptr<myvk::RenderPass> &render_pass,
	                   uint32_t subpass)
	    : m_dag_node_pool_ptr{dag_node_pool_ptr} {
		create_pipeline(render_pass, subpass);
	}
	void CmdDrawPipeline(const myvk::Ptr<myvk::CommandBuffer> &command_buffer, const Camera &camera, uint32_t width,
	                     uint32_t height, DAGRenderType render_type) const;
};

#endif // VKHASHDAG_DAGRENDERER_HPP

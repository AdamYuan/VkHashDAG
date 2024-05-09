//
// Created by adamyuan on 5/9/24.
//

#pragma once
#ifndef VKHASHDAG_RG_CROSSHAIRPASS_HPP
#define VKHASHDAG_RG_CROSSHAIRPASS_HPP

#include <myvk_rg/RenderGraph.hpp>

namespace rg {

class CrosshairPass final : public myvk_rg::GraphicsPassBase {
public:
	CrosshairPass(myvk_rg::Parent parent, const myvk_rg::Image &image);
	inline ~CrosshairPass() override = default;

	myvk::Ptr<myvk::GraphicsPipeline> CreatePipeline() const override;
	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const override;

	inline auto GetImageOutput() const { return MakeImageOutput({"image"}); }
};

} // namespace rg

#endif

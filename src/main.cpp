#include <myvk/FrameManager.hpp>
#include <myvk/GLFWHelper.hpp>
#include <myvk/ImGuiHelper.hpp>
#include <myvk/ImGuiRenderer.hpp>
#include <myvk/Instance.hpp>
#include <myvk/Queue.hpp>

#include "Camera.hpp"
#include "DAGNodePool.hpp"
#include "DAGRenderer.hpp"
#include "GPSQueueSelector.hpp"

#include <atomic>
#include <libfork/schedule/busy_pool.hpp>

constexpr uint32_t kFrameCount = 3;

bool cursor_captured = false;
void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
	if (action != GLFW_PRESS)
		return;
	if (key == GLFW_KEY_ESCAPE) {
		cursor_captured ^= 1;
		glfwSetInputMode(window, GLFW_CURSOR, cursor_captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}
}

struct AABBEditor {
	uint32_t level;
	hashdag::Vec3<uint32_t> aabb_min, aabb_max;
	inline hashdag::EditType EditNode(const hashdag::NodeCoord<uint32_t> &coord, hashdag::NodePointer<uint32_t>) const {
		auto lb = coord.GetLowerBoundAtLevel(level), ub = coord.GetUpperBoundAtLevel(level);
		/* printf("(%d %d %d), (%d, %d, %d) -> %d\n", lb.x, lb.y, lb.z, ub.x, ub.y, ub.z,
		       !ub.Any(std::less_equal<uint32_t>{}, aabb_min) && !lb.Any(std::greater_equal<uint32_t>{}, aabb_max)); */
		if (ub.Any(std::less_equal<uint32_t>{}, aabb_min) || lb.Any(std::greater_equal<uint32_t>{}, aabb_max))
			return hashdag::EditType::kNotAffected;
		if (lb.All(std::greater_equal<uint32_t>{}, aabb_min) && ub.All(std::less_equal<uint32_t>{}, aabb_max))
			return hashdag::EditType::kFill;
		return hashdag::EditType::kProceed;
	}
	inline bool EditVoxel(const hashdag::NodeCoord<uint32_t> &coord, bool voxel) const {
		/*if (coord.pos.All(std::greater_equal<uint32_t>{}, aabb_min) && coord.pos.All(std::less<uint32_t>{}, aabb_max))
		    printf("(%d %d %d)\n", coord.pos.x, coord.pos.y, coord.pos.z);
		*/
		return voxel || coord.pos.All(std::greater_equal<uint32_t>{}, aabb_min) &&
		                    coord.pos.All(std::less<uint32_t>{}, aabb_max);
	}
};

/* struct AABBIterator {
    uint32_t level;
    hashdag::Vec3<uint32_t> aabb_min, aabb_max;

    std::atomic_uint64_t count = 0;

    inline hashdag::IterateType IterateNode(const hashdag::NodeCoord<uint32_t> &coord,
                                            hashdag::NodePointer<uint32_t> node) const {
        auto lb = coord.GetLowerBoundAtLevel(level), ub = coord.GetUpperBoundAtLevel(level);
        if (!node || ub.Any(std::less_equal<uint32_t>{}, aabb_min) || lb.Any(std::greater_equal<uint32_t>{}, aabb_max))
            return hashdag::IterateType::kStop;
        return hashdag::IterateType::kProceed;
    }
    inline void IterateVoxel(const hashdag::NodeCoord<uint32_t> &coord, bool voxel) {
        if (voxel && coord.pos.All(std::greater_equal<uint32_t>{}, aabb_min) &&
            coord.pos.All(std::less<uint32_t>{}, aabb_max))
            count.fetch_add(1, std::memory_order_relaxed);
    }
}; */

template <typename Func> inline long ns(Func &&func) {
	auto begin = std::chrono::high_resolution_clock::now();
	func();
	auto end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

lf::busy_pool busy_pool(12);

int main() {
	GLFWwindow *window = myvk::GLFWCreateWindow("Test", 640, 480, true);
	glfwSetKeyCallback(window, key_callback);

	myvk::Ptr<myvk::Device> device;
	myvk::Ptr<myvk::Queue> generic_queue, sparse_queue;
	myvk::Ptr<myvk::PresentQueue> present_queue;
	{
		auto instance = myvk::Instance::CreateWithGlfwExtensions();
		auto surface = myvk::Surface::Create(instance, window);
		auto physical_device = myvk::PhysicalDevice::Fetch(instance)[0];
		device = myvk::Device::Create(
		    physical_device, GPSQueueSelector{&generic_queue, &sparse_queue, surface, &present_queue},
		    physical_device->GetDefaultFeatures(), {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME});
	}

	auto frame_manager = myvk::FrameManager::Create(generic_queue, present_queue, false, kFrameCount);

	myvk::Ptr<myvk::RenderPass> render_pass;
	{
		myvk::RenderPassState state{2, 1};
		state.RegisterAttachment(0, "color_attachment", frame_manager->GetSwapchain()->GetImageFormat(),
		                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_SAMPLE_COUNT_1_BIT,
		                         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);

		state.RegisterSubpass(0, "color_pass").AddDefaultColorAttachment("color_attachment", nullptr);
		state.RegisterSubpass(1, "gui_pass").AddDefaultColorAttachment("color_attachment", "color_pass");

		render_pass = myvk::RenderPass::Create(device, state);
	}

	auto dag_node_pool =
	    myvk::MakePtr<DAGNodePool>(generic_queue, sparse_queue, hashdag::Config<uint32_t>::MakeDefault(17, 9, 11, 0));

	// auto dag_node_pool = myvk::MakePtr<DAGNodePool>(generic_queue, sparse_queue,
	//                                                 hashdag::Config<uint32_t>::MakeDefault(17, 9, 14, 0, 7, 13));
	auto edit_ns = ns([&]() {
		dag_node_pool->SetRoot(dag_node_pool->EditLibFork(&busy_pool, dag_node_pool->GetRoot(),
		                                                  AABBEditor{
		                                                      .level = dag_node_pool->GetConfig().GetLowestLevel(),
		                                                      .aabb_min = {0, 0, 0},
		                                                      .aabb_max = {5000, 5000, 5000},
		                                                  },
		                                                  10));
		dag_node_pool->SetRoot(dag_node_pool->EditLibFork(&busy_pool, dag_node_pool->GetRoot(),
		                                                  AABBEditor{
		                                                      .level = dag_node_pool->GetConfig().GetLowestLevel(),
		                                                      .aabb_min = {1001, 1000, 1000},
		                                                      .aabb_max = {10000, 10000, 10000},
		                                                  },
		                                                  10));
	});
	printf("edit cost %lf ms\n", (double)edit_ns / 1000000.0);
	auto flush_ns = ns([&]() {
		auto fence = myvk::Fence::Create(device);
		dag_node_pool->Flush({}, {}, fence);
		fence->Wait();
	});
	printf("flush cost %lf ms\n", (double)flush_ns / 1000000.0);

	auto camera = myvk::MakePtr<Camera>();
	camera->m_speed = 0.25f;
	auto dag_renderer = myvk::MakePtr<DAGRenderer>(dag_node_pool, render_pass, 0);

	myvk::ImGuiInit(window, myvk::CommandPool::Create(generic_queue));

	auto imgui_renderer = myvk::ImGuiRenderer::Create(render_pass, 1, kFrameCount);

	auto framebuffer = myvk::ImagelessFramebuffer::Create(render_pass, {frame_manager->GetSwapchainImageViews()[0]});
	frame_manager->SetResizeFunc([&framebuffer, &render_pass, &frame_manager](const VkExtent2D &) {
		framebuffer = myvk::ImagelessFramebuffer::Create(render_pass, {frame_manager->GetSwapchainImageViews()[0]});
	});

	double prev_time = glfwGetTime();

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		double time = glfwGetTime(), delta = time - prev_time;
		prev_time = time;

		if (cursor_captured)
			camera->MoveControl(window, float(delta));

		myvk::ImGuiNewFrame();
		ImGui::Begin("Test");
		ImGui::Text("%f", ImGui::GetIO().Framerate);
		ImGui::End();
		ImGui::Render();

		if (frame_manager->NewFrame()) {
			uint32_t image_index = frame_manager->GetCurrentImageIndex();
			uint32_t current_frame = frame_manager->GetCurrentFrame();
			const auto &command_buffer = frame_manager->GetCurrentCommandBuffer();

			command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

			command_buffer->CmdBeginRenderPass(render_pass, {framebuffer},
			                                   {frame_manager->GetCurrentSwapchainImageView()},
			                                   {{{0.5f, 0.5f, 0.5f, 1.0f}}});
			dag_renderer->CmdDrawPipeline(command_buffer, *camera, //
			                              frame_manager->GetExtent().width, frame_manager->GetExtent().height);
			command_buffer->CmdNextSubpass();
			imgui_renderer->CmdDrawPipeline(command_buffer, current_frame);
			command_buffer->CmdEndRenderPass();

			command_buffer->End();

			frame_manager->Render();
		}
	}

	frame_manager->WaitIdle();
	glfwTerminate();
	return 0;
}

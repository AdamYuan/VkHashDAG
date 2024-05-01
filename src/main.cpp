#include <myvk/FrameManager.hpp>
#include <myvk/GLFWHelper.hpp>
#include <myvk/ImGuiHelper.hpp>
#include <myvk/ImGuiRenderer.hpp>
#include <myvk/Instance.hpp>
#include <myvk/Queue.hpp>

#include <hashdag/VBREditor.hpp>

#include "Camera.hpp"
#include "DAGColorPool.hpp"
#include "DAGNodePool.hpp"
#include "DAGRenderer.hpp"
#include "GPSQueueSelector.hpp"

#include <BS_thread_pool.hpp>
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
	glm::u32vec3 aabb_min, aabb_max;
	hashdag::VBRColor color;
	inline hashdag::EditType EditNode(const hashdag::Config<uint32_t> &config,
	                                  const hashdag::NodeCoord<uint32_t> &coord, hashdag::NodePointer<uint32_t>) const {
		auto lb = coord.GetLowerBoundAtLevel(config.GetVoxelLevel()),
		     ub = coord.GetUpperBoundAtLevel(config.GetVoxelLevel());
		/* printf("(%d %d %d), (%d, %d, %d) -> %d\n", lb.x, lb.y, lb.z, ub.x, ub.y, ub.z,
		       !ub.Any(std::less_equal<uint32_t>{}, aabb_min) && !lb.Any(std::greater_equal<uint32_t>{}, aabb_max)); */
		if (glm::any(glm::lessThanEqual(ub, aabb_min)) || glm::any(glm::greaterThanEqual(lb, aabb_max)))
			return hashdag::EditType::kNotAffected;
		if (glm::all(glm::greaterThanEqual(lb, aabb_min)) && glm::all(glm::lessThanEqual(ub, aabb_max)))
			return hashdag::EditType::kFill;
		return hashdag::EditType::kProceed;
	}
	inline hashdag::EditType EditNode(const hashdag::Config<uint32_t> &config,
	                                  const hashdag::NodeCoord<uint32_t> &coord, hashdag::NodePointer<uint32_t>,
	                                  hashdag::VBRColor &color) const {
		color = this->color;
		return EditNode(config, coord, {});
	}
	inline bool EditVoxel(const hashdag::Config<uint32_t> &config, const hashdag::NodeCoord<uint32_t> &coord,
	                      bool voxel) const {
		return voxel ||
		       glm::all(glm::greaterThanEqual(coord.pos, aabb_min)) && glm::all(glm::lessThan(coord.pos, aabb_max));
	}
	inline bool EditVoxel(const hashdag::Config<uint32_t> &config, const hashdag::NodeCoord<uint32_t> &coord,
	                      bool voxel, hashdag::VBRColor &color) const {
		color = this->color;
		return EditVoxel(config, coord, voxel);
	}
};

template <bool Fill = true> struct SphereEditor {
	glm::u32vec3 center;
	uint64_t r2;
	hashdag::VBRColor color;
	inline hashdag::EditType EditNode(const hashdag::Config<uint32_t> &config,
	                                  const hashdag::NodeCoord<uint32_t> &coord, hashdag::NodePointer<uint32_t>) const {
		auto lb = coord.GetLowerBoundAtLevel(config.GetVoxelLevel()),
		     ub = coord.GetUpperBoundAtLevel(config.GetVoxelLevel());
		glm::i64vec3 lb_dist = glm::i64vec3{lb} - glm::i64vec3(center);
		glm::i64vec3 ub_dist = glm::i64vec3{ub} - glm::i64vec3(center);
		glm::u64vec3 lb_dist_2 = lb_dist * lb_dist;
		glm::u64vec3 ub_dist_2 = ub_dist * ub_dist;

		glm::u64vec3 max_dist_2 = glm::max(lb_dist_2, ub_dist_2);
		uint64_t max_n2 = max_dist_2.x + max_dist_2.y + max_dist_2.z;
		if (max_n2 <= r2)
			return Fill ? hashdag::EditType::kFill : hashdag::EditType::kClear;

		uint64_t min_n2 = 0;
		if (lb_dist.x > 0)
			min_n2 += lb_dist_2.x;
		if (ub_dist.x < 0)
			min_n2 += ub_dist_2.x;
		if (lb_dist.y > 0)
			min_n2 += lb_dist_2.y;
		if (ub_dist.y < 0)
			min_n2 += ub_dist_2.y;
		if (lb_dist.z > 0)
			min_n2 += lb_dist_2.z;
		if (ub_dist.z < 0)
			min_n2 += ub_dist_2.z;

		return min_n2 > r2 ? hashdag::EditType::kNotAffected : hashdag::EditType::kProceed;
	}
	inline hashdag::EditType EditNode(const hashdag::Config<uint32_t> &config,
	                                  const hashdag::NodeCoord<uint32_t> &coord, hashdag::NodePointer<uint32_t>,
	                                  hashdag::VBRColor &color) const {
		color = this->color;
		return EditNode(config, coord, {});
	}
	inline bool EditVoxel(const hashdag::Config<uint32_t> &config, const hashdag::NodeCoord<uint32_t> &coord,
	                      bool voxel) const {
		auto p = coord.pos;
		glm::i64vec3 p_dist = glm::i64vec3{p.x, p.y, p.z} - glm::i64vec3(center);
		uint64_t p_n2 = p_dist.x * p_dist.x + p_dist.y * p_dist.y + p_dist.z * p_dist.z;
		if constexpr (Fill)
			return voxel || p_n2 <= r2;
		else
			return voxel && p_n2 > r2;
	}
	inline bool EditVoxel(const hashdag::Config<uint32_t> &config, const hashdag::NodeCoord<uint32_t> &coord,
	                      bool voxel, hashdag::VBRColor &color) const {
		color = this->color;
		return EditVoxel(config, coord, voxel);
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

BS::thread_pool edit_pool(1);
std::future<hashdag::NodePointer<uint32_t>> edit_future;

float edit_radius = 128.0f;
int render_type = 0;

int main() {
	GLFWwindow *window = myvk::GLFWCreateWindow("Test", 1280, 720, true);
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

	// auto dag_node_pool =
	//     myvk::MakePtr<DAGNodePool>(generic_queue, sparse_queue, hashdag::Config<uint32_t>::MakeDefault(16, 9, 11,
	//     0));

	auto dag_node_pool = myvk::MakePtr<DAGNodePool>(generic_queue, sparse_queue,
	                                                hashdag::Config<uint32_t>::MakeDefault(16, 9, 14, 2, 7, 11));
	auto dag_color_pool = myvk::MakePtr<DAGColorPool>(10);

	auto edit_ns = ns([&]() {
		const auto edit = [&](hashdag::VBREditor<uint32_t> auto &&vbr_editor) {
			using Editor_T = std::decay_t<decltype(vbr_editor)>;
			dag_node_pool->ThreadedEdit(&busy_pool, dag_node_pool->GetRoot(),
			                            hashdag::VBREditorWrapper<uint32_t, Editor_T, DAGColorPool>{
			                                .editor = std::forward<Editor_T>(vbr_editor),
			                                .p_octree = dag_color_pool.get(),
			                                .octree_root = dag_color_pool->GetRoot(),
			                            },
			                            dag_color_pool->GetLeafLevel(),
			                            [&](hashdag::NodePointer<uint32_t> root_ptr, auto &&state) {
				                            dag_node_pool->SetRoot(root_ptr);
				                            dag_color_pool->SetRoot(state.octree_node);
			                            });
		};
		edit(AABBEditor{
		    .aabb_min = {0, 0, 0},
		    .aabb_max = {5000, 5000, 5000},
		    .color = hashdag::RGB8Color{0xFF0000},
		});
		edit(AABBEditor{
		    .aabb_min = {1001, 1000, 1000},
		    .aabb_max = {10000, 10000, 10000},
		    .color = hashdag::RGB8Color{0xFF0000},
		});
		edit(SphereEditor<false>{
		    .center = {5005, 5000, 5000},
		    .r2 = 2000 * 2000,
		    .color = {},
		});
		edit(SphereEditor<false>{
		    .center = {10000, 10000, 10000},
		    .r2 = 4000 * 4000,
		    .color = {},
		});
	});
	printf("edit cost %lf ms\n", (double)edit_ns / 1000000.0);
	printf("root = %d\n", dag_color_pool->GetRoot().GetData());
	auto flush_ns = ns([&]() {
		auto fence = myvk::Fence::Create(device);
		if (dag_node_pool->Flush({}, {}, fence))
			fence->Wait();
	});
	printf("flush cost %lf ms\n", (double)flush_ns / 1000000.0);

	const auto pop_edit_result = [dag_node_pool]() {
		if (edit_future.valid() && edit_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			dag_node_pool->SetRoot(edit_future.get());
	};
	const auto push_edit = [dag_node_pool, device](const hashdag::Editor<uint32_t> auto &editor) {
		if (edit_future.valid())
			return;
		edit_future = edit_pool.submit_task([dag_node_pool, device, editor]() {
			hashdag::NodePointer<uint32_t> new_root_ptr;

			auto edit_ns = ns([&]() {
				new_root_ptr = dag_node_pool->ThreadedEdit(&busy_pool, dag_node_pool->GetRoot(), editor, 10);
			});
			printf("edit cost %lf ms\n", (double)edit_ns / 1000000.0);
			auto flush_ns = ns([&]() {
				auto fence = myvk::Fence::Create(device);
				if (dag_node_pool->Flush({}, {}, fence))
					fence->Wait();
			});
			printf("flush cost %lf ms\n", (double)flush_ns / 1000000.0);
			return new_root_ptr;
		});
	};

	auto camera = myvk::MakePtr<Camera>();
	camera->m_speed = 0.01f;
	auto dag_renderer = myvk::MakePtr<DAGRenderer>(dag_node_pool, render_pass, 0);

	myvk::ImGuiInit(window, myvk::CommandPool::Create(generic_queue));

	auto imgui_renderer = myvk::ImGuiRenderer::Create(render_pass, 1, kFrameCount);

	auto framebuffer = myvk::ImagelessFramebuffer::Create(render_pass, {frame_manager->GetSwapchainImageViews()[0]});
	frame_manager->SetResizeFunc([&framebuffer, &render_pass, &frame_manager](const VkExtent2D &) {
		framebuffer = myvk::ImagelessFramebuffer::Create(render_pass, {frame_manager->GetSwapchainImageViews()[0]});
	});

	double prev_time = glfwGetTime();

	while (!glfwWindowShouldClose(window)) {
		double time = glfwGetTime(), delta = time - prev_time;
		prev_time = time;

		glfwPollEvents();

		pop_edit_result();

		if (cursor_captured) {
			camera->MoveControl(window, float(delta));

			std::optional<glm::vec3> p =
			    dag_node_pool->Traversal<float>(dag_node_pool->GetRoot(), camera->m_position, camera->GetLook());
			if (p) {
				glm::u32vec3 up = *p * glm::vec3((float)dag_node_pool->GetConfig().GetResolution());
				auto r2 = uint64_t(edit_radius * edit_radius);

				if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
					push_edit(hashdag::StatelessEditorWrapper<uint32_t, SphereEditor<false>>{SphereEditor<false>{
					    .center = up,
					    .r2 = r2,
					}});
				} else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
					push_edit(hashdag::StatelessEditorWrapper<uint32_t, SphereEditor<>>{SphereEditor{
					    .center = up,
					    .r2 = r2,
					}});
				}
				// printf("%f %f %f\n", p->x, p->y, p->z);
			}
		}

		myvk::ImGuiNewFrame();
		ImGui::Begin("Test");
		ImGui::Text("FPS %f", ImGui::GetIO().Framerate);
		ImGui::DragFloat("Radius", &edit_radius, 1.0f, 0.0f, 2048.0f);
		ImGui::DragFloat("Speed", &camera->m_speed, 0.0001f, 0.0001f, 0.25f);
		ImGui::Combo("Type", &render_type, "Diffuse\0Normal\0Iteration\0");
		if (ImGui::Button("GC")) {
			auto gc_ns =
			    ns([&]() { dag_node_pool->SetRoot(dag_node_pool->ThreadedGC(&busy_pool, dag_node_pool->GetRoot())); });
			printf("GC cost %lf ms\n", (double)gc_ns / 1000000.0);
			auto flush_ns = ns([&]() {
				auto fence = myvk::Fence::Create(device);
				if (dag_node_pool->Flush({}, {}, fence))
					fence->Wait();
			});
			printf("flush cost %lf ms\n", (double)flush_ns / 1000000.0);
		}
		ImGui::End();
		ImGui::Render();

		if (frame_manager->NewFrame()) {
			uint32_t current_frame = frame_manager->GetCurrentFrame();
			const auto &command_buffer = frame_manager->GetCurrentCommandBuffer();

			command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

			command_buffer->CmdBeginRenderPass(render_pass, {framebuffer},
			                                   {frame_manager->GetCurrentSwapchainImageView()},
			                                   {{{0.5f, 0.5f, 0.5f, 1.0f}}});
			dag_renderer->CmdDrawPipeline(command_buffer, *camera, //
			                              frame_manager->GetExtent().width, frame_manager->GetExtent().height,
			                              static_cast<DAGRenderType>(render_type));
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

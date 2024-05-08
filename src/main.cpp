#include <myvk/FrameManager.hpp>
#include <myvk/GLFWHelper.hpp>
#include <myvk/ImGuiHelper.hpp>
#include <myvk/Instance.hpp>
#include <myvk/Queue.hpp>

#include <hashdag/VBREditor.hpp>

#include "Camera.hpp"
#include "DAGColorPool.hpp"
#include "DAGNodePool.hpp"
#include "GPSQueueSelector.hpp"
#include "rg/DAGRenderGraph.hpp"

#include <ThreadPool.h>
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
	                                  const hashdag::NodeCoord<uint32_t> &coord, hashdag::NodePointer<uint32_t> ptr,
	                                  hashdag::VBRColor &color, hashdag::VBRColorMode &color_mode) const {
		auto edit_type = EditNode(config, coord, {});
		if (edit_type == hashdag::EditType::kFill)
			color = this->color;
		else if (!ptr || color == this->color) {
			color = this->color;
			color_mode = hashdag::VBRColorMode::kFinal;
		}
		return edit_type;
	}
	inline bool VoxelInRange(const hashdag::NodeCoord<uint32_t> &coord) const {
		return glm::all(glm::greaterThanEqual(coord.pos, aabb_min)) && glm::all(glm::lessThan(coord.pos, aabb_max));
	}
	inline bool EditVoxel(const hashdag::Config<uint32_t> &config, const hashdag::NodeCoord<uint32_t> &coord,
	                      bool voxel) const {
		return voxel || VoxelInRange(coord);
	}
	inline bool EditVoxel(const hashdag::Config<uint32_t> &config, const hashdag::NodeCoord<uint32_t> &coord,
	                      bool voxel, hashdag::VBRColor &color) const {
		bool in_range = VoxelInRange(coord);
		color = in_range || !voxel ? this->color : color;
		return voxel || in_range;
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
	                                  const hashdag::NodeCoord<uint32_t> &coord, hashdag::NodePointer<uint32_t> ptr,
	                                  hashdag::VBRColor &color, hashdag::VBRColorMode &color_mode) const {
		auto edit_type = EditNode(config, coord, {});
		if (edit_type == hashdag::EditType::kFill)
			color = this->color;
		else if (!ptr || color == this->color) {
			color = this->color;
			color_mode = hashdag::VBRColorMode::kFinal;
		}
		return edit_type;
	}
	inline bool VoxelInRange(const hashdag::NodeCoord<uint32_t> &coord) const {
		auto p = coord.pos;
		glm::i64vec3 p_dist = glm::i64vec3{p.x, p.y, p.z} - glm::i64vec3(center);
		uint64_t p_n2 = p_dist.x * p_dist.x + p_dist.y * p_dist.y + p_dist.z * p_dist.z;
		return p_n2 <= r2;
	}
	inline bool EditVoxel(const hashdag::Config<uint32_t> &config, const hashdag::NodeCoord<uint32_t> &coord,
	                      bool voxel) const {
		bool in_range = VoxelInRange(coord);
		if constexpr (Fill)
			return voxel || in_range;
		else
			return voxel && !in_range;
	}
	inline bool EditVoxel(const hashdag::Config<uint32_t> &config, const hashdag::NodeCoord<uint32_t> &coord,
	                      bool voxel, hashdag::VBRColor &color) const {
		bool in_range = VoxelInRange(coord);
		if constexpr (Fill) {
			color = in_range || !voxel ? this->color : color;
			return voxel || in_range;
		} else
			return voxel && !in_range;
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

struct EditResult {
	hashdag::NodePointer<uint32_t> node_ptr;
	std::optional<DAGColorPool::Pointer> opt_color_ptr;
};

progschj::ThreadPool edit_pool(1);
std::future<EditResult> edit_future;

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

	auto dag_node_pool = DAGNodePool::Create(
	    hashdag::DefaultConfig<uint32_t>{
	        .level_count = 17,
	        .top_level_count = 9,
	        .word_bits_per_page = 14,
	        .page_bits_per_bucket = 2,
	        .bucket_bits_per_top_level = 7,
	        .bucket_bits_per_bottom_level = 11,
	    }(),
	    {generic_queue, sparse_queue});
	auto dag_color_pool = DAGColorPool::Create(
	    DAGColorPool::Config{
	        .leaf_level = 10,
	        .node_bits_per_node_page = 18,
	        .word_bits_per_leaf_page = 24,
	        .keep_history = false,
	    },
	    {generic_queue, sparse_queue});
	auto sparse_binder = myvk::MakePtr<VkSparseBinder>(sparse_queue);

	const auto edit = [&]<hashdag::Editor<uint32_t> Editor_T>(Editor_T &&editor) -> EditResult {
		return dag_node_pool->ThreadedEdit(&busy_pool, dag_node_pool->GetRoot(), std::forward<Editor_T>(editor),
		                                   dag_color_pool->GetLeafLevel(),
		                                   [&](hashdag::NodePointer<uint32_t> root_ptr, auto &&state) -> EditResult {
			                                   if constexpr (requires { state.octree_node; })
				                                   return {root_ptr, state.octree_node};
			                                   else
				                                   return {root_ptr, std::nullopt};
		                                   });
	};
	const auto vbr_edit = [&]<hashdag::VBREditor<uint32_t> VBREditor_T>(VBREditor_T &&vbr_editor) {
		return edit(hashdag::VBREditorWrapper<uint32_t, VBREditor_T, DAGColorPool>{
		    .editor = std::forward<VBREditor_T>(vbr_editor),
		    .p_octree = dag_color_pool.get(),
		    .octree_root = dag_color_pool->GetRoot(),
		});
	};
	const auto stateless_edit = [&]<hashdag::StatelessEditor<uint32_t> StatelessEditor_T>(StatelessEditor_T &&editor) {
		return edit(hashdag::StatelessEditorWrapper<uint32_t, StatelessEditor_T>{
		    .editor = std::forward<StatelessEditor_T>(editor)});
	};
	const auto gc = [&]() -> EditResult {
		return {.node_ptr = dag_node_pool->ThreadedGC(&busy_pool, dag_node_pool->GetRoot()),
		        .opt_color_ptr = std::nullopt};
	};
	const auto set_root = [&](const EditResult &edit_result) {
		dag_node_pool->SetRoot(edit_result.node_ptr);
		if (edit_result.opt_color_ptr)
			dag_color_pool->SetRoot(*edit_result.opt_color_ptr);
	};
	const auto flush = [&]() {
		dag_node_pool->Flush(sparse_binder);
		dag_color_pool->Flush(sparse_binder);
		auto fence = myvk::Fence::Create(device);
		if (sparse_binder->QueueBind({}, {}, fence) == VK_SUCCESS)
			fence->Wait();
	};

	{
		auto edit_ns = ns([&]() {
			set_root(vbr_edit(AABBEditor{
			    .aabb_min = {1001, 1000, 1000},
			    .aabb_max = {10000, 10000, 10000},
			    .color = hashdag::RGB8Color{0xFFFF00},
			}));
			set_root(vbr_edit(AABBEditor{
			    .aabb_min = {0, 0, 0},
			    .aabb_max = {5000, 5000, 5000},
			    .color = hashdag::RGB8Color{0x00FFFF},
			}));
			set_root(vbr_edit(SphereEditor<true>{
			    .center = {5005, 5000, 5000},
			    .r2 = 2000 * 2000,
			    .color = hashdag::RGB8Color{0x0000FF},
			}));
			set_root(stateless_edit(SphereEditor<false>{
			    .center = {10000, 10000, 10000},
			    .r2 = 4000 * 4000,
			    .color = {},
			}));
		});
		printf("edit cost %lf ms\n", (double)edit_ns / 1000000.0);
		printf("root = %d\n", dag_color_pool->GetRoot().GetData());
		auto flush_ns = ns([&]() { flush(); });
		printf("flush cost %lf ms\n", (double)flush_ns / 1000000.0);
	}

	const auto pop_edit_result = [&]() {
		if (edit_future.valid() && edit_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			set_root(edit_future.get());
	};
	const auto push_edit = [&]<typename Editor_T>(auto &&edit_func, Editor_T &&editor) {
		if (edit_future.valid())
			return;
		edit_future = edit_pool.enqueue([&]() {
			EditResult result;
			auto edit_ns = ns([&]() { result = edit_func(std::forward<Editor_T>(editor)); });
			printf("edit cost %lf ms\n", (double)edit_ns / 1000000.0);
			auto flush_ns = ns([&]() { flush(); });
			printf("flush cost %lf ms\n", (double)flush_ns / 1000000.0);
			return result;
		});
	};

	auto camera = myvk::MakePtr<Camera>();
	camera->m_speed = 0.01f;

	myvk::ImGuiInit(window, myvk::CommandPool::Create(generic_queue));

	std::array<myvk::Ptr<rg::DAGRenderGraph>, kFrameCount> render_graphs;
	for (auto &rg : render_graphs)
		rg = myvk::MakePtr<rg::DAGRenderGraph>(frame_manager, camera, dag_node_pool, dag_color_pool);

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
					push_edit(stateless_edit, SphereEditor<false>{
					                              .center = up,
					                              .r2 = r2,
					                          });
				} else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
					push_edit(vbr_edit, SphereEditor{
					                        .center = up,
					                        .r2 = r2,
					                        .color = hashdag::VBRColor{0x00FF00},
					                    });
				}
			}
		}

		myvk::ImGuiNewFrame();
		ImGui::Begin("Test");
		ImGui::Text("FPS %f", ImGui::GetIO().Framerate);
		ImGui::DragFloat("Radius", &edit_radius, 1.0f, 0.0f, 2048.0f);
		ImGui::DragFloat("Speed", &camera->m_speed, 0.0001f, 0.0001f, 0.25f);
		ImGui::Combo("Type", &render_type, "Diffuse\0Normal\0Iteration\0");
		if (ImGui::Button("GC")) {
			auto gc_ns = ns([&]() { set_root(gc()); });
			printf("GC cost %lf ms\n", (double)gc_ns / 1000000.0);
			auto flush_ns = ns([&]() { flush(); });
			printf("flush cost %lf ms\n", (double)flush_ns / 1000000.0);
		}
		ImGui::End();
		ImGui::Render();

		if (frame_manager->NewFrame()) {
			uint32_t current_frame = frame_manager->GetCurrentFrame();
			auto &render_graph = render_graphs[frame_manager->GetCurrentFrame()];

			const auto &command_buffer = frame_manager->GetCurrentCommandBuffer();

			command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			render_graph->SetRenderType(render_type);
			render_graph->SetCanvasSize(frame_manager->GetExtent());
			render_graph->CmdExecute(command_buffer);
			command_buffer->End();

			frame_manager->Render();
		}
	}

	frame_manager->WaitIdle();
	glfwTerminate();
	return 0;
}

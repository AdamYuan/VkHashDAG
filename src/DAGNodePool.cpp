//
// Created by adamyuan on 1/15/24.
//

#include "DAGNodePool.hpp"

void DAGNodePool::create_vk_buffer() {
	m_paged_buffer = VkPagedBuffer::Create(
	    m_device_ptr, (VkDeviceSize)GetConfig().GetTotalWords() * sizeof(uint32_t),
	    [this](const VkMemoryRequirements &mem_req) {
		    m_page_bits_per_gpu_page =
		        mem_req.alignment <= GetConfig().GetWordsPerPage() * sizeof(uint32_t)
		            ? 0
		            : std::countr_zero(mem_req.alignment / sizeof(uint32_t)) - GetConfig().word_bits_per_page;
		    return (1u << (GetConfig().word_bits_per_page + m_page_bits_per_gpu_page)) * sizeof(uint32_t);
	    },
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, {m_main_queue_ptr, m_sparse_queue_ptr});
}

void DAGNodePool::create_vk_descriptor() {
	VkDescriptorSetLayoutBinding layout_binding = {};
	layout_binding.binding = 0;
	layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	layout_binding.descriptorCount = 1;
	layout_binding.stageFlags = VK_SHADER_STAGE_ALL;

	auto descriptor_set_layout = myvk::DescriptorSetLayout::Create(m_device_ptr, {layout_binding});
	auto descriptor_pool = myvk::DescriptorPool::Create(m_device_ptr, 1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}});
	m_descriptor_set = myvk::DescriptorSet::Create(descriptor_pool, descriptor_set_layout);

	m_descriptor_set->UpdateStorageBuffer(m_paged_buffer, 0);
}

/* template <typename Func> inline long ns(Func &&func) {
    auto begin = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
} */

bool DAGNodePool::Flush(const myvk::SemaphoreGroup &wait_semaphores, const myvk::SemaphoreGroup &signal_semaphores,
                        const myvk::Ptr<myvk::Fence> &fence) {
	phmap::flat_hash_set<uint32_t> alloc_gpu_pages, free_gpu_pages;

	// auto scan0_ns = ns([&]() {
	for (const auto &it : m_page_write_ranges) {
		uint32_t page_id = it.first, gpu_page_id = page_id >> m_page_bits_per_gpu_page;
		if (!m_paged_buffer->IsPageExist(gpu_page_id))
			alloc_gpu_pages.insert(gpu_page_id);
	}

	for (uint32_t page_id : m_page_frees) {
		uint32_t gpu_page_id = page_id >> m_page_bits_per_gpu_page;

		uint32_t page_begin = gpu_page_id << m_page_bits_per_gpu_page;
		uint32_t page_end = (gpu_page_id + 1u) << m_page_bits_per_gpu_page;

		if (m_paged_buffer->IsPageExist(gpu_page_id) &&
		    std::all_of(m_pages.get() + page_begin, m_pages.get() + page_end, [](auto &p) { return p == nullptr; }))
			free_gpu_pages.insert(gpu_page_id);
	}
	// printf("%zu GPU pages deleted\n", free_gpu_pages.size());
	/* });
	printf("scan0 %lf ms\n", (double)scan0_ns / 1000000.0); */

	bool binded = m_paged_buffer->QueueBindSparse(m_sparse_queue_ptr, alloc_gpu_pages, free_gpu_pages, wait_semaphores,
	                                              signal_semaphores, fence) == VK_SUCCESS;

	// auto scan1_ns = ns([&]() {
	for (const auto &it : m_page_write_ranges) {
		uint32_t page_id = it.first, gpu_page_id = page_id >> m_page_bits_per_gpu_page;
		const Range &range = it.second;
		uint32_t gpu_page_offset =
		    ((page_id & ((1u << m_page_bits_per_gpu_page) - 1)) << GetConfig().word_bits_per_page) | range.begin;
		std::copy(m_pages[page_id].get() + range.begin, m_pages[page_id].get() + range.end,
		          m_paged_buffer->GetMappedPage<uint32_t>(gpu_page_id) + gpu_page_offset);
	}
	/* });
	printf("scan1 %lf ms\n", (double)scan1_ns / 1000000.0); */

	m_page_write_ranges.clear();
	m_page_frees.clear();

	return binded;
}

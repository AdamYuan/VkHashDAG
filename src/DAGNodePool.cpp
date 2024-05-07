//
// Created by adamyuan on 1/15/24.
//

#include "DAGNodePool.hpp"

#include <cassert>

myvk::Ptr<DAGNodePool> DAGNodePool::Create(hashdag::Config<uint32_t> config,
                                           const std::vector<myvk::Ptr<myvk::Queue>> &queues) {
	if (queues.empty())
		return nullptr;

	auto device = queues[0]->GetDevicePtr();
	auto buffer = VkPagedBuffer::Create(
	    device, (VkDeviceSize)config.GetTotalWords() * sizeof(uint32_t),
	    [&](const VkMemoryRequirements &mem_req) -> VkDeviceSize {
		    assert(std::popcount(mem_req.alignment) == 1);

		    uint32_t word_bits_per_alignment =
		        std::bit_width(std::max(mem_req.alignment / sizeof(uint32_t), (VkDeviceSize)1)) - 1u;
		    if (word_bits_per_alignment > config.word_bits_per_page) {
			    uint32_t d = word_bits_per_alignment - config.word_bits_per_page;
			    if (config.page_bits_per_bucket < d)
				    return 0; // Failed to fit size
			    config.page_bits_per_bucket -= d;
			    config.word_bits_per_page += d;
		    }

		    return config.GetWordsPerPage() * sizeof(uint32_t);
	    },
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, queues);

	if (buffer == nullptr)
		return nullptr;

	printf("%zu %u\n", buffer->GetPageSize() / sizeof(uint32_t), config.GetWordsPerPage());

	return myvk::MakePtr<DAGNodePool>(std::move(config), std::move(buffer));
}

void DAGNodePool::create_vk_descriptor() {
	VkDescriptorSetLayoutBinding layout_binding = {};
	layout_binding.binding = 0;
	layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	layout_binding.descriptorCount = 1;
	layout_binding.stageFlags = VK_SHADER_STAGE_ALL;

	const auto &device = GetDevicePtr();
	auto descriptor_set_layout = myvk::DescriptorSetLayout::Create(device, {layout_binding});
	auto descriptor_pool = myvk::DescriptorPool::Create(device, 1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}});
	m_descriptor_set = myvk::DescriptorSet::Create(descriptor_pool, descriptor_set_layout);

	m_descriptor_set->UpdateStorageBuffer(m_buffer, 0);
}

/* template <typename Func> inline long ns(Func &&func) {
    auto begin = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
} */

void DAGNodePool::Flush(const myvk::Ptr<VkSparseBinder> &binder) {
	phmap::flat_hash_set<uint32_t> alloc_gpu_pages, free_gpu_pages;

	// auto scan0_ns = ns([&]() {
	for (const auto &[page_id, _] : m_page_write_ranges)
		if (!m_buffer->IsPageExist(page_id))
			alloc_gpu_pages.insert(page_id);

	for (uint32_t page_id : m_page_frees)
		if (m_buffer->IsPageExist(page_id) && m_pages[page_id] == nullptr)
			free_gpu_pages.insert(page_id);

	// printf("%zu GPU pages deleted\n", free_gpu_pages.size());
	/* });
	printf("scan0 %lf ms\n", (double)scan0_ns / 1000000.0); */

	m_buffer->Alloc(binder, alloc_gpu_pages);
	m_buffer->Free(binder, free_gpu_pages);

	// auto scan1_ns = ns([&]() {
	for (const auto &[page_id, range] : m_page_write_ranges) {
		auto *p_page = m_pages[page_id].get();
		std::copy(p_page + range.begin, p_page + range.end, m_buffer->GetMappedPage<uint32_t>(page_id) + range.begin);
	}
	/* });
	printf("scan1 %lf ms\n", (double)scan1_ns / 1000000.0); */

	m_page_write_ranges.clear();
	m_page_frees.clear();
}

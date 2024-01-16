//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_VKNODEPOOL_HPP
#define VKHASHDAG_VKNODEPOOL_HPP

#include <hashdag/NodePool.hpp>
#include <memory>
#include <myvk/Buffer.hpp>
#include <myvk/DescriptorSet.hpp>
#include <myvk/Fence.hpp>
#include <myvk/Semaphore.hpp>

class DAGNodePool final : public hashdag::NodePoolBase<DAGNodePool, uint32_t, hashdag::MurmurHasher32> {
private:
	std::vector<uint32_t> m_bucket_words;
	std::vector<std::unique_ptr<uint32_t[]>> m_pages;

	// Functions for hashdag::NodePoolBase
	friend class hashdag::NodePoolBase<DAGNodePool, uint32_t, hashdag::MurmurHasher32>;

	inline uint32_t GetBucketWords(uint32_t bucket_id) const { return m_bucket_words[bucket_id]; }
	inline void SetBucketWords(uint32_t bucket_id, uint32_t words) { m_bucket_words[bucket_id] = words; }
	inline const uint32_t *ReadPage(uint32_t page_id) const { return m_pages[page_id].get(); }
	inline void ZeroPage(uint32_t page_id, uint32_t page_offset, uint32_t zero_words) {
		std::fill(m_pages[page_id].get() + page_offset, m_pages[page_id].get() + page_offset + zero_words, 0);
	}
	inline void WritePage(uint32_t page_id, uint32_t page_offset, std::span<const uint32_t> word_span) {
		if (!m_pages[page_id])
			m_pages[page_id] = std::make_unique_for_overwrite<uint32_t[]>(GetConfig().GetWordsPerPage());
		std::copy(word_span.begin(), word_span.end(), m_pages[page_id].get() + page_offset);
		m_gpu_write_ranges[page_id].Union({page_offset, page_offset + (uint32_t)word_span.size()});
	}

	// Root
	hashdag::NodePointer<uint32_t> m_root{};

	// GPU stuff
	myvk::Ptr<myvk::Device> m_device_ptr;
	myvk::Ptr<myvk::Queue> m_main_queue_ptr, m_sparse_queue_ptr;

	VkBuffer m_gpu_buffer{VK_NULL_HANDLE};
	VkDeviceSize m_gpu_buffer_size{};
	VkMemoryRequirements m_gpu_page_memory_requirements;
	struct GPUPage {
		VmaAllocation allocation;
		uint32_t *p_mapped_data;
	};
	std::vector<GPUPage> m_gpu_pages;
	struct Range {
		uint32_t begin = -1, end = 0;
		inline void Union(Range r) {
			begin = std::min(begin, r.begin);
			end = std::max(end, r.end);
		}
	};
	std::unordered_map<uint32_t, Range> m_gpu_write_ranges;

	uint32_t m_page_bits_per_gpu_page;

	myvk::Ptr<myvk::DescriptorSet> m_descriptor_set;

	void create_buffer();
	void create_descriptor();
	void create_gpu_pages();
	void destroy_buffer();

public:
	inline DAGNodePool(const myvk::Ptr<myvk::Queue> &main_queue_ptr, const myvk::Ptr<myvk::Queue> &sparse_queue_ptr,
	                   const hashdag::Config<uint32_t> &config)
	    : hashdag::NodePoolBase<DAGNodePool, uint32_t, hashdag::MurmurHasher32>(config),
	      m_device_ptr{main_queue_ptr->GetDevicePtr()}, m_main_queue_ptr{main_queue_ptr},
	      m_sparse_queue_ptr{sparse_queue_ptr} {

		m_bucket_words.resize(GetConfig().GetTotalBuckets());
		m_pages.resize(GetConfig().GetTotalPages());

		create_buffer();
		create_gpu_pages();
		create_descriptor();
	}
	inline ~DAGNodePool() final { destroy_buffer(); }

	void Flush(const myvk::SemaphoreGroup &wait_semaphores, const myvk::SemaphoreGroup &signal_semaphores,
	           const myvk::Ptr<myvk::Fence> &fence);

	inline void SetRoot(hashdag::NodePointer<uint32_t> root) { m_root = root; }
	inline auto GetRoot() const { return m_root; }

	inline const auto &GetDescriptorSet() const { return m_descriptor_set; }
	inline const auto &GetDescriptorSetLayout() const { return m_descriptor_set->GetDescriptorSetLayoutPtr(); }
};

#endif // VKHASHDAG_VKNODEPOOL_HPP

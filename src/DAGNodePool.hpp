//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_VKNODEPOOL_HPP
#define VKHASHDAG_VKNODEPOOL_HPP

#include <atomic>
#include <cuckoohash_map.hh>
#include <memory>

#include <hashdag/NodePool.hpp>
#include <hashdag/NodePoolThreadedEdit.hpp>
#include <hashdag/NodePoolTraversal.hpp>

#include <myvk/Buffer.hpp>
#include <myvk/DescriptorSet.hpp>
#include <myvk/Fence.hpp>
#include <myvk/Semaphore.hpp>

class DAGNodePool final : public hashdag::NodePoolBase<DAGNodePool, uint32_t>,
                          public hashdag::NodePoolThreadedEdit<DAGNodePool, uint32_t>,
                          public hashdag::NodePoolTraversal<DAGNodePool, uint32_t> {
public:
	using WordSpanHasher = hashdag::MurmurHasher32;

private:
	struct Range {
		uint32_t begin = -1, end = 0;
		inline void Union(Range r) {
			begin = std::min(begin, r.begin);
			end = std::max(end, r.end);
		}
	};

	std::unique_ptr<std::atomic_uint32_t[]> m_bucket_words;
	std::unique_ptr<std::unique_ptr<uint32_t[]>[]> m_pages;
	std::array<hashdag::EditMutex, 1024> m_edit_mutexes{};
	libcuckoo::cuckoohash_map<uint32_t, Range> m_page_write_ranges;

	// Functions for hashdag::NodePoolBase
	friend class hashdag::NodePoolBase<DAGNodePool, uint32_t>;

	inline hashdag::EditMutex &GetBucketEditMutex(uint32_t bucket_id) {
		return m_edit_mutexes[bucket_id % m_edit_mutexes.size()];
	}
	inline uint32_t GetBucketWords(uint32_t bucket_id) const {
		return m_bucket_words[bucket_id].load(std::memory_order_acquire);
	}
	inline uint32_t GetBucketWordsAtomic(uint32_t bucket_id) const {
		return m_bucket_words[bucket_id].load(std::memory_order_acquire);
	}
	inline void SetBucketWords(uint32_t bucket_id, uint32_t words) {
		m_bucket_words[bucket_id].store(words, std::memory_order_release);
	}
	inline void SetBucketWordsAtomic(uint32_t bucket_id, uint32_t words) {
		m_bucket_words[bucket_id].store(words, std::memory_order_release);
	}
	inline const uint32_t *ReadPage(uint32_t page_id) const { return m_pages[page_id].get(); }
	inline void ZeroPage(uint32_t page_id, uint32_t page_offset, uint32_t zero_words) {
		std::fill(m_pages[page_id].get() + page_offset, m_pages[page_id].get() + page_offset + zero_words, 0);
	}
	inline void WritePage(uint32_t page_id, uint32_t page_offset, std::span<const uint32_t> word_span) {
		if (!m_pages[page_id])
			m_pages[page_id] = std::make_unique_for_overwrite<uint32_t[]>(GetConfig().GetWordsPerPage());
		std::copy(word_span.begin(), word_span.end(), m_pages[page_id].get() + page_offset);

		/* Range range = {page_offset, page_offset + (uint32_t)word_span.size()};
		uint32_t gpu_page_id = page_id >> m_page_bits_per_gpu_page;
		if (m_gpu_pages[gpu_page_id].p_mapped_data) {
		    const auto &gpu_page = m_gpu_pages[gpu_page_id];
		    uint32_t gpu_page_offset =
		        ((page_id & ((1u << m_page_bits_per_gpu_page) - 1)) << GetConfig().word_bits_per_page) | range.begin;
		    std::copy(m_pages[page_id].get() + range.begin, m_pages[page_id].get() + range.end,
		              gpu_page.p_mapped_data + gpu_page_offset);
		} else */
		m_page_write_ranges.upsert(page_id, [&](Range &r, libcuckoo::UpsertContext) {
			r.Union({page_offset, page_offset + (uint32_t)word_span.size()});
		});
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
	std::unique_ptr<GPUPage[]> m_gpu_pages;
	uint32_t m_total_gpu_pages, m_page_bits_per_gpu_page;

	myvk::Ptr<myvk::DescriptorSet> m_descriptor_set;

	void create_buffer();
	void create_descriptor();
	void create_gpu_pages();
	void destroy_buffer();

public:
	inline DAGNodePool(const myvk::Ptr<myvk::Queue> &main_queue_ptr, const myvk::Ptr<myvk::Queue> &sparse_queue_ptr,
	                   const hashdag::Config<uint32_t> &config)
	    : hashdag::NodePoolBase<DAGNodePool, uint32_t>(config), m_device_ptr{main_queue_ptr->GetDevicePtr()},
	      m_main_queue_ptr{main_queue_ptr}, m_sparse_queue_ptr{sparse_queue_ptr} {

		m_bucket_words = std::make_unique<std::atomic_uint32_t[]>(GetConfig().GetTotalBuckets());
		m_pages = std::make_unique<std::unique_ptr<uint32_t[]>[]>(GetConfig().GetTotalPages());

		create_buffer();
		create_gpu_pages();
		create_descriptor();
	}
	inline ~DAGNodePool() final { destroy_buffer(); }

	// return true if need to insert missing pages
	bool Flush(const myvk::SemaphoreGroup &wait_semaphores, const myvk::SemaphoreGroup &signal_semaphores,
	           const myvk::Ptr<myvk::Fence> &fence);

	inline void SetRoot(hashdag::NodePointer<uint32_t> root) { m_root = root; }
	inline auto GetRoot() const { return m_root; }

	inline const auto &GetDescriptorSet() const { return m_descriptor_set; }
	inline const auto &GetDescriptorSetLayout() const { return m_descriptor_set->GetDescriptorSetLayoutPtr(); }
};

#endif // VKHASHDAG_VKNODEPOOL_HPP

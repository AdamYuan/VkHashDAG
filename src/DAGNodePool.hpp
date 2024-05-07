//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_VKNODEPOOL_HPP
#define VKHASHDAG_VKNODEPOOL_HPP

#include <atomic>
#include <memory>
#include <parallel_hashmap/phmap.h>

#include <hashdag/NodePool.hpp>
#include <hashdag/NodePoolThreadedEdit.hpp>
#include <hashdag/NodePoolThreadedGC.hpp>
#include <hashdag/NodePoolTraversal.hpp>

#include <myvk/Buffer.hpp>
#include <myvk/DescriptorSet.hpp>
#include <myvk/Fence.hpp>
#include <myvk/Semaphore.hpp>

#include "Range.hpp"
#include "VkPagedBuffer.hpp"

class DAGNodePool final
    : public hashdag::NodePoolBase<DAGNodePool, uint32_t>,
      public hashdag::NodePoolTraversal<DAGNodePool, uint32_t>,
      public hashdag::NodePoolThreadedEdit<DAGNodePool, uint32_t>,
      public hashdag::NodePoolThreadedGC<DAGNodePool, uint32_t, phmap::flat_hash_map, phmap::flat_hash_set> {
public:
	using WordSpanHasher = hashdag::MurmurHasher32;

private:
	std::unique_ptr<std::atomic_uint32_t[]> m_bucket_words;
	std::unique_ptr<std::unique_ptr<uint32_t[]>[]> m_pages;
	std::array<std::mutex, 1024> m_edit_mutexes{};
	phmap::parallel_flat_hash_map<uint32_t, Range<uint32_t>, std::hash<uint32_t>, std::equal_to<>,
	                              std::allocator<std::pair<uint32_t, Range<uint32_t>>>, 6, std::mutex>
	    m_page_write_ranges;
	phmap::parallel_flat_hash_set<uint32_t, std::hash<uint32_t>, std::equal_to<>, std::allocator<uint32_t>, 6,
	                              std::mutex>
	    m_page_frees;

	// Functions for hashdag::NodePoolBase
	friend class hashdag::NodePoolBase<DAGNodePool, uint32_t>;

	inline std::mutex &GetBucketMutex(uint32_t bucket_id) { return m_edit_mutexes[bucket_id % m_edit_mutexes.size()]; }
	inline uint32_t GetBucketWords(uint32_t bucket_id) const {
		return m_bucket_words[bucket_id].load(std::memory_order_acquire);
	}
	inline void SetBucketWords(uint32_t bucket_id, uint32_t words) {
		m_bucket_words[bucket_id].store(words, std::memory_order_release);
	}
	inline const uint32_t *ReadPage(uint32_t page_id) const { return m_pages[page_id].get(); }
	inline void ZeroPage(uint32_t page_id, uint32_t page_offset, uint32_t zero_words) {
		std::fill(m_pages[page_id].get() + page_offset, m_pages[page_id].get() + page_offset + zero_words, 0);
	}
	inline void FreePage(uint32_t page_id) {
		m_pages[page_id] = nullptr;
		m_page_frees.insert(page_id);
	}
	inline void WritePage(uint32_t page_id, uint32_t page_offset, std::span<const uint32_t> word_span) {
		if (!m_pages[page_id])
			m_pages[page_id] = std::make_unique_for_overwrite<uint32_t[]>(GetConfig().GetWordsPerPage());
		std::copy(word_span.begin(), word_span.end(), m_pages[page_id].get() + page_offset);
		Range range = {page_offset, page_offset + (uint32_t)word_span.size()};
		m_page_write_ranges.lazy_emplace_l(
		    page_id, [&](auto &it) { it.second.Union(range); }, [&](const auto &ctor) { ctor(page_id, range); });
	}

	// Root
	hashdag::NodePointer<uint32_t> m_root{};

	// GPU stuff
	myvk::Ptr<myvk::Device> m_device_ptr;
	myvk::Ptr<myvk::Queue> m_main_queue_ptr, m_sparse_queue_ptr;

	myvk::Ptr<VkPagedBuffer> m_paged_buffer;
	uint32_t m_page_bits_per_gpu_page;

	myvk::Ptr<myvk::DescriptorSet> m_descriptor_set;

	void create_vk_buffer();
	void create_vk_descriptor();

public:
	DAGNodePool(const myvk::Ptr<myvk::Queue> &main_queue_ptr, const myvk::Ptr<myvk::Queue> &sparse_queue_ptr,
	            const hashdag::Config<uint32_t> &config)
	    : hashdag::NodePoolBase<DAGNodePool, uint32_t>(config), m_device_ptr{main_queue_ptr->GetDevicePtr()},
	      m_main_queue_ptr{main_queue_ptr}, m_sparse_queue_ptr{sparse_queue_ptr} {

		m_bucket_words = std::make_unique<std::atomic_uint32_t[]>(GetConfig().GetTotalBuckets());
		m_pages = std::make_unique<std::unique_ptr<uint32_t[]>[]>(GetConfig().GetTotalPages());

		create_vk_buffer();
		create_vk_descriptor();
	}
	inline ~DAGNodePool() final = default;

	// return true if need to insert missing pages
	bool Flush(const myvk::SemaphoreGroup &wait_semaphores, const myvk::SemaphoreGroup &signal_semaphores,
	           const myvk::Ptr<myvk::Fence> &fence);

	inline void SetRoot(hashdag::NodePointer<uint32_t> root) { m_root = root; }
	inline auto GetRoot() const { return m_root; }

	inline const auto &GetDescriptorSet() const { return m_descriptor_set; }
	inline const auto &GetDescriptorSetLayout() const { return m_descriptor_set->GetDescriptorSetLayoutPtr(); }
};

#endif // VKHASHDAG_VKNODEPOOL_HPP

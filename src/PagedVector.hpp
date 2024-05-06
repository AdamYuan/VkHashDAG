//
// Created by adamyuan on 5/5/24.
//

#pragma once
#ifndef PAGEDVECTOR_HPP
#define PAGEDVECTOR_HPP

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

template <typename T, typename Derived> class PagedVectorBase {
protected:
	std::unique_ptr<std::unique_ptr<T[]>[]> m_pages;
	std::size_t m_page_count{}, m_page_size{}, m_page_bits{}, m_page_mask{};

private:
	inline void foreach_page(std::size_t idx, std::size_t count,
	                         std::invocable<std::size_t, std::size_t, std::size_t, std::size_t> auto &&func) const {
		std::size_t offset = 0, page_id = idx >> m_page_bits, page_offset = idx & m_page_mask;
		for (; count >= m_page_size - page_offset;
		     offset += m_page_size - page_offset, count -= m_page_size - page_offset, ++page_id, page_offset = 0)
			func(offset, page_id, page_offset, m_page_size - page_offset);

		if (count)
			func(offset, page_id, page_offset, count);
	}

	template <typename, typename> friend class PagedSpan;

public:
	using Type = T;

	inline PagedVectorBase() = default;
	inline PagedVectorBase(std::size_t page_count, std::size_t bits_per_page)
	    : m_page_count{page_count}, m_page_bits{bits_per_page}, m_page_size{std::size_t(1) << bits_per_page},
	      m_page_mask{(std::size_t(1) << bits_per_page) - 1} {
		m_pages = std::make_unique<std::unique_ptr<T[]>[]>(page_count);
	}
	inline std::optional<std::size_t> Append(std::invocable<T &> auto &&appender) {
		std::size_t idx = static_cast<Derived *>(this)->append_one();
		if (idx >= (m_page_count << m_page_bits))
			return std::nullopt;
		T *p_page = static_cast<Derived *>(this)->upsert_page(idx >> m_page_bits);
		appender(p_page[idx & m_page_mask]);
		return idx;
	}
	inline std::optional<std::size_t> Append(std::size_t count,
	                                         std::invocable<std::size_t, std::span<T>> auto &&appender) {
		std::size_t idx = static_cast<Derived *>(this)->append_count(count);
		if (idx + count > (m_page_count << m_page_bits))
			return std::nullopt;
		foreach_page(
		    idx, count,
		    [this, &appender](std::size_t offset, std::size_t page_id, std::size_t page_offset, std::size_t page_size) {
			    T *p_page = static_cast<Derived *>(this)->upsert_page(page_id);
			    appender(offset, std::span<T>(p_page + page_offset, page_size));
		    });
		return idx;
	}

	// Read or Write must happen after [idx] is Appended
	inline auto Read(std::size_t idx, std::invocable<const T &> auto &&reader) const {
		return reader(m_pages[idx >> m_page_bits][idx & m_page_mask]);
	}
	inline void Read(std::size_t idx, std::size_t count,
	                 std::invocable<std::size_t, std::span<const T>> auto &&reader) const {
		foreach_page(
		    idx, count,
		    [this, &reader](std::size_t offset, std::size_t page_id, std::size_t page_offset, std::size_t page_size) {
			    reader(offset, std::span<const T>(m_pages[page_id].get() + page_offset, page_size));
		    });
	}
	inline auto Write(std::size_t idx, std::invocable<T &> auto &&writer) {
		return writer(m_pages[idx >> m_page_bits][idx & m_page_mask]);
	}
	inline void Write(std::size_t idx, std::size_t count, std::invocable<std::size_t, std::span<T>> auto &&writer) {
		foreach_page(
		    idx, count,
		    [this, &writer](std::size_t offset, std::size_t page_id, std::size_t page_offset, std::size_t page_size) {
			    writer(offset, std::span<T>(m_pages[page_id].get() + page_offset, page_size));
		    });
	}
};

template <typename T> class PagedVector : public PagedVectorBase<T, PagedVector<T>> {
	std::size_t m_count{};

	inline std::size_t append_one() { return m_count++; }
	inline std::size_t append_count(std::size_t count) {
		auto ret = m_count;
		m_count += count;
		return ret;
	}
	inline T *upsert_page(std::size_t page_id) {
		if (this->m_pages[page_id] == nullptr)
			this->m_pages[page_id] = std::make_unique_for_overwrite<T[]>(this->m_page_size);
		return this->m_pages[page_id].get();
	}
	template <typename, typename> friend class PagedVectorBase;

public:
	inline PagedVector() = default;
	inline PagedVector(std::size_t page_count, std::size_t bits_per_page)
	    : PagedVectorBase<T, PagedVector>(page_count, bits_per_page) {}
};

template <typename T> class SafePagedVector : public PagedVectorBase<T, SafePagedVector<T>> {
	inline static constexpr std::size_t kPageMutexCountBits = 4;
	std::atomic_size_t m_atomic_count{};
	std::mutex m_page_mutices[1 << kPageMutexCountBits];

	inline std::size_t append_one() { return m_atomic_count.fetch_add(1, std::memory_order_relaxed); }
	inline std::size_t append_count(std::size_t count) {
		return m_atomic_count.fetch_add(count, std::memory_order_relaxed);
	}
	inline T *upsert_page(std::size_t page_id) {
		std::scoped_lock lock{m_page_mutices[page_id & ((1 << kPageMutexCountBits) - 1)]};
		if (this->m_pages[page_id] == nullptr)
			this->m_pages[page_id] = std::make_unique_for_overwrite<T[]>(this->m_page_size);
		return this->m_pages[page_id].get();
	}
	template <typename, typename> friend class PagedVectorBase;

public:
	inline SafePagedVector() = default;
	inline SafePagedVector(std::size_t page_count, std::size_t bits_per_page)
	    : PagedVectorBase<T, SafePagedVector>(page_count, bits_per_page) {}
};

template <typename PagedVector_T, typename View_T = typename PagedVector_T::Type> class PagedSpan {
	using Store_T = typename PagedVector_T::Type;
	static_assert(sizeof(View_T) % sizeof(Store_T) == 0);

	inline static constexpr std::size_t kRatio = sizeof(View_T) / sizeof(Store_T),
	                                    kRatioBits = std::bit_width(kRatio) - 1;
	static_assert((1u << kRatioBits) == kRatio);

	std::size_t m_offset{}, m_count{}, m_view_offset{}, m_view_count{}, m_page_bits{}, m_page_mask{};
	const std::unique_ptr<Store_T[]> *m_pages;

public:
	inline PagedSpan(const PagedVector_T &vector, std::size_t offset, std::size_t count)
	    : m_pages{vector.m_pages.get()}, m_page_bits{vector.m_page_bits}, m_page_mask{vector.m_page_mask}, //
	      m_offset{offset}, m_count{count},                                                                //
	      m_view_offset{offset >> kRatioBits}, m_view_count{count >> kRatioBits} {
		static_assert(std::is_base_of_v<PagedVectorBase<Store_T, PagedVector_T>, PagedVector_T>);
		assert(offset % kRatio == 0 && count % kRatio == 0);
	}
	inline const View_T &operator[](std::size_t view_idx) const {
		std::size_t idx = (view_idx + m_view_offset) << kRatioBits;
		return *((const View_T *)(m_pages[idx >> m_page_bits].get() + (idx & m_page_mask)));
	}
	inline std::size_t size() const { return m_view_count; }
};

#endif // PAGEDVECTOR_HPP

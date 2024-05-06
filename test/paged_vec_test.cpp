#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <PagedVector.hpp>
#include <cinttypes>
#include <array>

using Vec4u = std::array<uint32_t, 4>;

TEST_CASE("Test PagedVector") {
	constexpr std::size_t kPageCount = 16, kPageBits = 5;
	PagedVector<uint32_t> vec{kPageCount, kPageBits};

	auto opt_idx = vec.Append([](uint32_t &i) { i = 0; });
	CHECK(opt_idx.has_value());
	CHECK(*opt_idx == 0);
	opt_idx = vec.Append((kPageCount << kPageBits) - 3, [](std::size_t offset, std::span<uint32_t> span) {
		for (uint32_t v = offset; uint32_t & i : span)
			i = ++v;
	});
	CHECK(opt_idx.has_value());
	CHECK(*opt_idx == 1);
	opt_idx = vec.Append([](uint32_t &i) { i = (kPageCount << kPageBits) - 2; });
	CHECK(opt_idx.has_value());
	CHECK(*opt_idx == (kPageCount << kPageBits) - 2);
	opt_idx = vec.Append([](uint32_t &i) { i = (kPageCount << kPageBits) - 1; });
	CHECK(opt_idx.has_value());
	CHECK(*opt_idx == (kPageCount << kPageBits) - 1);
	opt_idx = vec.Append([](uint32_t &i) { i = 300; });
	CHECK(!opt_idx.has_value());

	vec.Read(0, kPageCount << kPageBits, [](std::size_t offset, std::span<const uint32_t> span) {
		for (uint32_t v = offset; uint32_t i : span) {
			CHECK_EQ(i, v);
			++v;
		}
	});

	PagedSpan<PagedVector<uint32_t>, Vec4u> span_4u{vec, 4, (kPageCount << kPageBits) - 8};
	CHECK_EQ(span_4u.size(), (kPageCount << kPageBits) / 4 - 2);
	CHECK_EQ(span_4u[0], Vec4u{4, 5, 6, 7});
}

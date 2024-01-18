//
// Created by adamyuan on 1/18/24.
//

#pragma once
#ifndef VKHASHDAG_NODEPOOLTRAVERSALGLM_HPP
#define VKHASHDAG_NODEPOOLTRAVERSALGLM_HPP

#include "NodePool.hpp"

#include <bit>
#include <concepts>
#include <glm/glm.hpp>
#include <numeric>
#include <optional>

namespace hashdag {

template <typename Derived, std::unsigned_integral Word> class NodePoolGLMTraversal {
private:
	inline const auto &get_node_pool() const {
		return *static_cast<const NodePoolBase<Derived, Word> *>(static_cast<const Derived *>(this));
	}
	inline auto &get_node_pool() { return *static_cast<NodePoolBase<Derived, Word> *>(static_cast<Derived *>(this)); }

	template <std::floating_point F> inline static Word float_bits_to_word(F f) {
		static_assert(sizeof(F) == sizeof(Word));
		union {
			F f_;
			Word w_;
		} u;
		static_assert(sizeof(u) == sizeof(Word));
		u.f_ = f;
		return u.w_;
	}

	template <std::floating_point F> inline static F word_bits_to_float(Word w) {
		static_assert(sizeof(F) == sizeof(Word));
		union {
			F f_;
			Word w_;
		} u;
		static_assert(sizeof(u) == sizeof(Word));
		u.w_ = w;
		return u.f_;
	}

	template <std::floating_point F> inline static F fast_exp2(Word w) {
		static constexpr Word kFractionBits = std::numeric_limits<F>::digits - 1;
		static constexpr Word kExponentBias = std::numeric_limits<F>::max_exponent - 1;
		return word_bits_to_float<F>((kExponentBias + w) << kFractionBits);
	}

	inline Word DAG_GetLeafFirstChildBits(Word node) const {
		const Word *p = get_node_pool().read_node(node);
		Word l0 = p[0], l1 = p[1];
		return ((l0 & 0x000000FFu) == 0u ? 0u : 0x01u) | ((l0 & 0x0000FF00u) == 0u ? 0u : 0x02u) |
		       ((l0 & 0x00FF0000u) == 0u ? 0u : 0x04u) | ((l0 & 0xFF000000u) == 0u ? 0u : 0x08u) |
		       ((l1 & 0x000000FFu) == 0u ? 0u : 0x10u) | ((l1 & 0x0000FF00u) == 0u ? 0u : 0x20u) |
		       ((l1 & 0x00FF0000u) == 0u ? 0u : 0x40u) | ((l1 & 0xFF000000u) == 0u ? 0u : 0x80u);
	}

	template <std::floating_point F> struct StackItem {
		Word node;
		F t_max;
	};

public:
	inline NodePoolGLMTraversal() { static_assert(std::is_base_of_v<NodePoolBase<Derived, Word>, Derived>); }

	/*
	 *  Copyright (c) 2009-2011, NVIDIA Corporation
	 *  All rights reserved.
	 *
	 *  Redistribution and use in source and binary forms, with or without
	 *  modification, are permitted provided that the following conditions are met:
	 *      * Redistributions of source code must retain the above copyright
	 *        notice, this list of conditions and the following disclaimer.
	 *      * Redistributions in binary form must reproduce the above copyright
	 *        notice, this list of conditions and the following disclaimer in the
	 *        documentation and/or other materials provided with the distribution.
	 *      * Neither the name of NVIDIA Corporation nor the
	 *        names of its contributors may be used to endorse or promote products
	 *        derived from this software without specific prior written permission.
	 *
	 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
	 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	 *  DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
	 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
	 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
	 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
	 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
	 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
	 */

	template <std::floating_point F, glm::qualifier Q = glm::defaultp>
	inline std::optional<glm::vec<3, F, Q>> GLMTraversal(NodePointer<Word> root_ptr, glm::vec<3, F, Q> o,
	                                                     glm::vec<3, F, Q> d) const {
		static_assert(std::numeric_limits<F>::is_iec559);

		using Vec3 = glm::vec<3, F, Q>;

		if (!root_ptr)
			return std::nullopt;

		static constexpr F kEpsilon = std::numeric_limits<F>::epsilon();
		static constexpr std::size_t kStackSize = std::numeric_limits<F>::digits - 1; // Fraction bits

		std::array<StackItem<F>, kStackSize> stack;

		o += F{1};

		d.x = glm::abs(d.x) > kEpsilon ? d.x : (d.x >= 0 ? kEpsilon : -kEpsilon);
		d.y = glm::abs(d.y) > kEpsilon ? d.y : (d.y >= 0 ? kEpsilon : -kEpsilon);
		d.z = glm::abs(d.z) > kEpsilon ? d.z : (d.z >= 0 ? kEpsilon : -kEpsilon);

		// Precompute the coefficients of tx(x), ty(y), and tz(z).
		// The octree is assumed to reside at coordinates [1, 2].
		Vec3 t_coef = F{1} / -glm::abs(d);
		Vec3 t_bias = t_coef * o;

		Word octant_mask = 0u;
		if (d.x > 0)
			octant_mask |= 1u, t_bias.x = F{3} * t_coef.x - t_bias.x;
		if (d.y > 0)
			octant_mask |= 2u, t_bias.y = F{3} * t_coef.y - t_bias.y;
		if (d.z > 0)
			octant_mask |= 4u, t_bias.z = F{3} * t_coef.z - t_bias.z;

		// Initialize the active span of t-values.
		F t_min =
		    glm::max(glm::max(F{2} * t_coef.x - t_bias.x, F{2} * t_coef.y - t_bias.y), F{2} * t_coef.z - t_bias.z);
		F t_max = glm::min(glm::min(t_coef.x - t_bias.x, t_coef.y - t_bias.y), t_coef.z - t_bias.z);
		F h = t_max;
		t_min = glm::max(t_min, F{0});
		t_max = glm::min(t_max, F{1});

		Word parent = *root_ptr, child_bits = 0u;
		Vec3 pos = Vec3(F{1});

		Word idx = 0u;
		if (F{1.5} * t_coef.x - t_bias.x > t_min)
			idx |= 1u, pos.x = F{1.5};
		if (F{1.5} * t_coef.y - t_bias.y > t_min)
			idx |= 2u, pos.y = F{1.5};
		if (F{1.5} * t_coef.z - t_bias.z > t_min)
			idx |= 4u, pos.z = F{1.5};

		Word scale = kStackSize - 1;
		F scale_exp2 = 0.5; // exp2( scale - STACK_SIZE )

		const Word kLeafScale = kStackSize - get_node_pool().m_config.GetNodeLevels();

		while (scale < kStackSize) {
			if (child_bits == 0u)
				child_bits = scale > kLeafScale ? *get_node_pool().read_node(parent)
				                                : (scale == kLeafScale ? DAG_GetLeafFirstChildBits(parent) : parent);
			// Determine maximum t-value of the cube by evaluating
			// tx(), ty(), and tz() at its corner.

			Vec3 t_corner = pos * t_coef - t_bias;
			F tc_max = glm::min(glm::min(t_corner.x, t_corner.y), t_corner.z);

			Word child_shift = idx ^ octant_mask; // permute child slots based on the mirroring
			Word child_mask = 1u << child_shift;

			if ((child_bits & child_mask) != 0 && t_min <= t_max) {
				// INTERSECT
				F tv_max = glm::min(t_max, tc_max);
				F half_scale_exp2 = scale_exp2 * 0.5;
				Vec3 t_center = half_scale_exp2 * t_coef + t_corner;

				if (t_min <= tv_max) {
					if (scale < kLeafScale || scale == -1) // leaf node
						break;

					// PUSH
					if (tc_max < h) {
						stack[scale].node = parent;
						stack[scale].t_max = t_max;
					}
					h = tc_max;

					parent =
					    scale > kLeafScale
					        ? *get_node_pool().read_node(parent + 1u + std::popcount(child_bits & (child_mask - 1)))
					        : 0xffu & (*get_node_pool().read_node(parent + (child_shift >> 2u)) >>
					                   ((child_shift & 3u) << 3u));

					idx = 0u;
					--scale;
					scale_exp2 = half_scale_exp2;
					if (t_center.x > t_min)
						idx ^= 1u, pos.x += scale_exp2;
					if (t_center.y > t_min)
						idx ^= 2u, pos.y += scale_exp2;
					if (t_center.z > t_min)
						idx ^= 4u, pos.z += scale_exp2;

					child_bits = 0;
					t_max = tv_max;

					continue;
				}
			}

			// ADVANCE
			Word step_mask = 0u;
			if (t_corner.x <= tc_max)
				step_mask |= 1u, pos.x -= scale_exp2;
			if (t_corner.y <= tc_max)
				step_mask |= 2u, pos.y -= scale_exp2;
			if (t_corner.z <= tc_max)
				step_mask |= 4u, pos.z -= scale_exp2;

			// Update active t-span and flip bits of the child slot index.
			t_min = tc_max;
			idx ^= step_mask;

			// Proceed with pop if the bit flips disagree with the ray direction.
			if (idx & step_mask) {
				// POP
				// Find the highest differing bit between the two positions.
				Word differing_bits = 0;
				if (step_mask & 1u)
					differing_bits |= float_bits_to_word<F>(pos.x) ^ float_bits_to_word<F>(pos.x + scale_exp2);
				if (step_mask & 2u)
					differing_bits |= float_bits_to_word<F>(pos.y) ^ float_bits_to_word<F>(pos.y + scale_exp2);
				if (step_mask & 4u)
					differing_bits |= float_bits_to_word<F>(pos.z) ^ float_bits_to_word<F>(pos.z + scale_exp2);
				scale = glm::findMSB(differing_bits);
				scale_exp2 = fast_exp2<F>(scale - kStackSize);

				// Restore parent voxel from the stack.
				parent = stack[scale].node;
				t_max = stack[scale].t_max;

				// Round cube position and extract child slot index.
				Word shx = float_bits_to_word<F>(pos.x) >> scale;
				Word shy = float_bits_to_word<F>(pos.y) >> scale;
				Word shz = float_bits_to_word<F>(pos.z) >> scale;
				pos.x = word_bits_to_float<F>(shx << scale);
				pos.y = word_bits_to_float<F>(shy << scale);
				pos.z = word_bits_to_float<F>(shz << scale);
				idx = (shx & 1u) | ((shy & 1u) << 1u) | ((shz & 1u) << 2u);

				// Prevent same parent from being stored again and invalidate cached
				// child descriptor.
				h = F{0};
				child_bits = 0u;
			}
		}

		// Undo mirroring of the coordinate system.
		if (octant_mask & 1u)
			pos.x = F{3} - scale_exp2 - pos.x;
		if (octant_mask & 2u)
			pos.y = F{3} - scale_exp2 - pos.y;
		if (octant_mask & 4u)
			pos.z = F{3} - scale_exp2 - pos.z;

		return scale < kStackSize && t_min <= t_max
		           ? std::optional<Vec3>{glm::clamp(o + t_min * d, pos, pos + scale_exp2) - F{1}}
		           : std::nullopt;
	}
};

} // namespace hashdag

#endif // VKHASHDAG_NODEPOOLTRAVERSALGLM_HPP

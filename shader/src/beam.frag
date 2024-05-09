#version 460
#extension GL_EXT_control_flow_attributes : enable

layout(std430, binding = 0) readonly buffer uuDAGNodes { uint uDAGNodes[]; };

layout(location = 0) out float oT;

layout(push_constant) uniform uuPushConstant {
	float uPosX, uPosY, uPosZ, uLookX, uLookY, uLookZ, uSideX, uSideY, uSideZ, uUpX, uUpY, uUpZ;
	uint uWidth, uHeight;
	uint uDAGRoot, uDAGLeafLevel;
	float uProjectionFactor;
};

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

// STACK_SIZE equals to the fraction bits of float
#define STACK_SIZE 23
struct StackItem {
	uint node;
	float t_max;
} stack[STACK_SIZE + 1];

uint DAG_GetLeafFirstChildBits(in const uint node) {
	/* uvec2 l = uvec2(uDAGNodes[node], uDAGNodes[node + 1]);
	l |= l >> 1;
	l |= l >> 2;
	l |= l >> 4;
	l &= 0x01010101u; */
	uint l0 = uDAGNodes[node], l1 = uDAGNodes[node + 1];
	return ((l0 & 0x000000FFu) == 0u ? 0u : 0x01u) | ((l0 & 0x0000FF00u) == 0u ? 0u : 0x02u) |
	       ((l0 & 0x00FF0000u) == 0u ? 0u : 0x04u) | ((l0 & 0xFF000000u) == 0u ? 0u : 0x08u) |
	       ((l1 & 0x000000FFu) == 0u ? 0u : 0x10u) | ((l1 & 0x0000FF00u) == 0u ? 0u : 0x20u) |
	       ((l1 & 0x00FF0000u) == 0u ? 0u : 0x40u) | ((l1 & 0xFF000000u) == 0u ? 0u : 0x80u);
}

bool DAG_RayMarch(in const uint root,
                  in const uint leaf_level,
                  in const float proj_factor,
                  vec3 o,
                  vec3 d,
                  out float o_t,
                  out float o_vox_scale) {
	if (root == -1)
		return false;

	o += 1.0;
	const float epsilon = uintBitsToFloat((127u - STACK_SIZE) << 23u); // exp2f(-STACK_SIZE)
	d.x = abs(d.x) > epsilon ? d.x : (d.x >= 0 ? epsilon : -epsilon);
	d.y = abs(d.y) > epsilon ? d.y : (d.y >= 0 ? epsilon : -epsilon);
	d.z = abs(d.z) > epsilon ? d.z : (d.z >= 0 ? epsilon : -epsilon);

	// Precompute the coefficients of tx(x), ty(y), and tz(z).
	// The octree is assumed to reside at coordinates [1, 2].
	vec3 t_coef = 1.0 / -abs(d);
	vec3 t_bias = t_coef * o;

	uint octant_mask = 0u;
	if (d.x > 0.0f)
		octant_mask ^= 1u, t_bias.x = 3.0 * t_coef.x - t_bias.x;
	if (d.y > 0.0f)
		octant_mask ^= 2u, t_bias.y = 3.0 * t_coef.y - t_bias.y;
	if (d.z > 0.0f)
		octant_mask ^= 4u, t_bias.z = 3.0 * t_coef.z - t_bias.z;

	// Initialize the active span of t-values.
	float t_min = max(max(2.0 * t_coef.x - t_bias.x, 2.0 * t_coef.y - t_bias.y), 2.0 * t_coef.z - t_bias.z);
	float t_max = min(min(t_coef.x - t_bias.x, t_coef.y - t_bias.y), t_coef.z - t_bias.z);
	float h = t_max;
	t_min = max(t_min, 0.0);
	t_max = min(t_max, 1.0);

	uint parent = root, child_bits = 0u;
	vec3 pos = vec3(1.0);
	uint idx = 0u;
	if (1.5 * t_coef.x - t_bias.x > t_min)
		idx ^= 1u, pos.x = 1.5;
	if (1.5 * t_coef.y - t_bias.y > t_min)
		idx ^= 2u, pos.y = 1.5;
	if (1.5 * t_coef.z - t_bias.z > t_min)
		idx ^= 4u, pos.z = 1.5;

	uint scale = STACK_SIZE - 1;
	float scale_exp2 = 0.5; // exp2( scale - STACK_SIZE )

	const uint leaf_scale = STACK_SIZE - leaf_level;

	while (scale < STACK_SIZE) {
		if (child_bits == 0u)
			child_bits = scale > leaf_scale ? uDAGNodes[parent]
			                                : (scale == leaf_scale ? DAG_GetLeafFirstChildBits(parent) : parent);
		// Determine maximum t-value of the cube by evaluating
		// tx(), ty(), and tz() at its corner.

		vec3 t_corner = pos * t_coef - t_bias;
		float tc_max = min(min(t_corner.x, t_corner.y), t_corner.z);

		uint child_shift = idx ^ octant_mask; // permute child slots based on the mirroring
		uint child_mask = 1u << child_shift;

		if ((child_bits & child_mask) != 0 && t_min <= t_max) {
			// INTERSECT
			float tv_max = min(t_max, tc_max);
			float half_scale_exp2 = scale_exp2 * 0.5;
			vec3 t_center = half_scale_exp2 * t_coef + t_corner;

			if (t_min <= tv_max) {
				if (scale < leaf_scale || scale_exp2 * proj_factor < tc_max)
					break;

				// PUSH
				if (tc_max < h) {
					stack[scale].node = parent;
					stack[scale].t_max = t_max;
				}
				h = tc_max;

				parent = scale > leaf_scale
				             ? uDAGNodes[parent + 1 + bitCount(child_bits & (child_mask - 1))]
				             : (uDAGNodes[parent + (child_shift >> 2u)] >> ((child_shift & 3u) << 3u)) & 0xFFu;

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
		uint step_mask = 0u;
		if (t_corner.x <= tc_max)
			step_mask ^= 1u, pos.x -= scale_exp2;
		if (t_corner.y <= tc_max)
			step_mask ^= 2u, pos.y -= scale_exp2;
		if (t_corner.z <= tc_max)
			step_mask ^= 4u, pos.z -= scale_exp2;

		// Update active t-span and flip bits of the child slot index.
		t_min = tc_max;
		idx ^= step_mask;

		// Proceed with pop if the bit flips disagree with the ray direction.
		if ((idx & step_mask) != 0) {
			// POP
			// Find the highest differing bit between the two positions.
			uint differing_bits = 0;
			if ((step_mask & 1u) != 0)
				differing_bits |= floatBitsToUint(pos.x) ^ floatBitsToUint(pos.x + scale_exp2);
			if ((step_mask & 2u) != 0)
				differing_bits |= floatBitsToUint(pos.y) ^ floatBitsToUint(pos.y + scale_exp2);
			if ((step_mask & 4u) != 0)
				differing_bits |= floatBitsToUint(pos.z) ^ floatBitsToUint(pos.z + scale_exp2);
			scale = findMSB(differing_bits);
			scale_exp2 = uintBitsToFloat((scale - STACK_SIZE + 127u) << 23u); // exp2f(scale - s_max)

			// Restore parent voxel from the stack.
			parent = stack[scale].node;
			t_max = stack[scale].t_max;

			// Round cube position and extract child slot index.
			uint shx = floatBitsToUint(pos.x) >> scale;
			uint shy = floatBitsToUint(pos.y) >> scale;
			uint shz = floatBitsToUint(pos.z) >> scale;
			pos.x = uintBitsToFloat(shx << scale);
			pos.y = uintBitsToFloat(shy << scale);
			pos.z = uintBitsToFloat(shz << scale);
			idx = (shx & 1u) | ((shy & 1u) << 1u) | ((shz & 1u) << 2u);

			// Prevent same parent from being stored again and invalidate cached
			// child descriptor.
			h = 0.0;
			child_bits = 0;
		}
	}

	o_t = t_min;
	o_vox_scale = scale_exp2;
	return scale < STACK_SIZE && t_min <= t_max;
}

vec3 Camera_GenRay() {
	vec2 coord = gl_FragCoord.xy / vec2(uWidth, uHeight);
	coord = coord * 2.0f - 1.0f;
	return normalize(vec3(uLookX, uLookY, uLookZ) - vec3(uSideX, uSideY, uSideZ) * coord.x -
	                 vec3(uUpX, uUpY, uUpZ) * coord.y);
}

void main() {
	vec3 o = vec3(uPosX, uPosY, uPosZ), d = Camera_GenRay();

	float t, vox_scale;
	bool hit = DAG_RayMarch(uDAGRoot, uDAGLeafLevel, uProjectionFactor, o, d, t, vox_scale);
	t = hit ? max(t - vox_scale, 0.0) : uintBitsToFloat(0x7F800000u /* inf */);

	oT = t;
}

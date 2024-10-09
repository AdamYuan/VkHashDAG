#version 460
#extension GL_EXT_control_flow_attributes : enable

layout(std430, binding = 0) readonly buffer uuDAGNodes { uint uDAGNodes[]; };

layout(std430, binding = 1) readonly buffer uuColorNodes { uint uColorNodes[]; };
layout(std430, binding = 2) readonly buffer uuColorLeaves { uint uColorLeaves[]; };

#ifdef BEAM_OPTIMIZATION
layout(binding = 3) uniform sampler2D uBeam;
#endif

layout(location = 0) out vec4 oColor;

layout(push_constant) uniform uuPushConstant {
	float uPosX, uPosY, uPosZ, uLookX, uLookY, uLookZ, uSideX, uSideY, uSideZ, uUpX, uUpY, uUpZ;
	uint uWidth, uHeight;
	uint uVoxelLevel;
	uint uDAGRoot, uDAGLeafLevel;
	uint uColorRoot, uColorLeafLevel;
	float uProjectionFactor;
	uint uType;
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
uint stack[STACK_SIZE];

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
                  in const float proj_bias,
                  vec3 o,
                  vec3 d,
                  out vec3 o_hit_pos,
                  out vec3 o_norm,
                  out uvec3 o_vox_pos,
                  out uint o_vox_size,
                  out vec3 o_vox_min,
                  out vec3 o_vox_max,
                  out uint o_iter) {
	if (root == -1)
		return false;

	uint iter = 0;

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

	for (;;) {
		++iter;

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
			float half_scale_exp2 = scale_exp2 * 0.5;
			vec3 t_center = half_scale_exp2 * t_coef + t_corner;

			if (scale < leaf_scale || scale_exp2 * proj_factor < tc_max + proj_bias)
				break;

			// PUSH
			if (tc_max < h)
				stack[scale] = parent;
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

			continue;
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
			if (scale >= STACK_SIZE)
				break;
			scale_exp2 = uintBitsToFloat((scale - STACK_SIZE + 127u) << 23u); // exp2f(scale - s_max)

			// Restore parent voxel from the stack.
			parent = stack[scale];

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

	vec3 t_corner = t_coef * (pos + scale_exp2) - t_bias;

	// normal
	vec3 norm = (t_corner.x > t_corner.y && t_corner.x > t_corner.z)
	                ? vec3(-1, 0, 0)
	                : (t_corner.y > t_corner.z ? vec3(0, -1, 0) : vec3(0, 0, -1));
	if ((octant_mask & 1u) == 0u)
		norm.x = -norm.x;
	if ((octant_mask & 2u) == 0u)
		norm.y = -norm.y;
	if ((octant_mask & 4u) == 0u)
		norm.z = -norm.z;

	const uint voxel_level = leaf_level + 1u, voxel_scale = STACK_SIZE - voxel_level;

	// voxel size & position
	uint vox_size = 1u << (scale - voxel_scale);
	uvec3 vox_pos = (floatBitsToUint(pos) & 0x7FFFFFu) >> voxel_scale;
	if ((octant_mask & 1u) != 0u)
		vox_pos.x = (1u << voxel_level) - vox_size - vox_pos.x;
	if ((octant_mask & 2u) != 0u)
		vox_pos.y = (1u << voxel_level) - vox_size - vox_pos.y;
	if ((octant_mask & 4u) != 0u)
		vox_pos.z = (1u << voxel_level) - vox_size - vox_pos.z;

	// float voxel bounds & hit position
	vec3 vox_min = uintBitsToFloat(vox_pos << voxel_scale | 127u << 23u),
	     vox_max = uintBitsToFloat((vox_pos + vox_size) << voxel_scale | 127u << 23u);
	vec3 hit_pos = clamp(o + t_min * d, vox_min, vox_max);
	if (norm.x != 0)
		hit_pos.x = norm.x > 0 ? vox_max.x + epsilon * 2 : vox_min.x - epsilon;
	if (norm.y != 0)
		hit_pos.y = norm.y > 0 ? vox_max.y + epsilon * 2 : vox_min.y - epsilon;
	if (norm.z != 0)
		hit_pos.z = norm.z > 0 ? vox_max.z + epsilon * 2 : vox_min.z - epsilon;
	hit_pos -= 1.0, vox_min -= 1.0, vox_max -= 1.0;

	// Output
	o_hit_pos = hit_pos;
	o_norm = norm;
	o_vox_pos = vox_pos;
	o_vox_size = vox_size;
	o_vox_min = vox_min;
	o_vox_max = vox_max;
	o_iter = iter;

	return scale < STACK_SIZE && t_min <= t_max;
}

uint Color_Morton32(uvec3 u) {
	u = (u | (u << 16u)) & 0x030000FFu;
	u = (u | (u << 8u)) & 0x0300F00Fu;
	u = (u | (u << 4u)) & 0x030C30C3u;
	u = (u | (u << 2u)) & 0x09249249u;
	return u.x | (u.y << 1u) | (u.z << 2u);
}

uvec2 Color_GetU2(in const uint ofst, in const uint idx) {
	// ofst is guarenteed to be a multiple of 2
	uint p = ofst + (idx << 1u);
	return uvec2(uColorLeaves[p], uColorLeaves[p | 1u]);
}
uint Color_GetU2_x(in const uint ofst, in const uint idx) { return uColorLeaves[ofst + (idx << 1u)]; }
uint Color_GetU2_y(in const uint ofst, in const uint idx) { return uColorLeaves[(ofst + (idx << 1u)) | 1u]; }
uint Color_GetWeight(in const uint ofst, in const uint bit_idx, in const uint bits_per_weight) {
	uint bit_ofst = bit_idx & 31u;
	bool single = bit_ofst + bits_per_weight <= 32; // Is the weight in a single word?
	uint w0 = uColorLeaves[ofst + (bit_idx >> 5u)] >> bit_ofst;
	if (single) {
		w0 &= (1u << bits_per_weight) - 1u;
		return w0;
	}
	uint w1 = uColorLeaves[ofst + (bit_idx >> 5u) + 1u];
	w1 &= (1u << (bit_ofst + bits_per_weight - 32u)) - 1u;
	return w0 | (w1 << (32u - bit_ofst));
}
vec3 Color_UnpackRGB565(in const uint c) {
	uvec3 c3 = (uvec3(c) >> uvec3(0u, 5u, 11u)) & uvec3(0x1Fu, 0x3Fu, 0x1Fu);
	return vec3(c3) / vec3(0x1Fu, 0x3Fu, 0x1Fu);
}

vec3 Color_GetLeafColor(in const uint idx, in const uvec3 vox_pos) {
	uint macro_cnt = uColorLeaves[idx + 1u], block_cnt = uColorLeaves[idx + 2u];
	uint macro_offset = idx + 4u;
	uint block_offset = macro_offset + (macro_cnt << 1u);
	uint weight_offset = block_offset + (block_cnt << 1u);

	uint vox_id = Color_Morton32(vox_pos);
	uint macro_id = vox_id >> 14u;
	if (macro_id >= macro_cnt)
		return vec3(0);
	uvec2 macro = Color_GetU2(macro_offset, macro_id);

	// Refine Block Range and Vox ID
	block_offset += macro.x << 1u;
	block_cnt = macro_id + 1u < macro_cnt ? Color_GetU2_x(macro_offset, macro_id + 1u) - macro.x : block_cnt - macro.x;
	vox_id &= 0x3FFFu;

	// Binary Search for Block, Find the first block with vox_idx_offset > vox_id
	if (block_cnt == 0u)
		return vec3(0);

	[[unroll]] for (uint _ = 0; _ <= 14u && block_cnt != 0u; ++_) {
		uint step = block_cnt >> 1u;
		if ((Color_GetU2_y(block_offset, step) >> 18u) <= vox_id) {
			block_cnt -= (step + 1u);
			block_offset += (step + 1u) << 1u;
		} else
			block_cnt = step;
	}
	block_offset -= 2u;

	// Read Color
	uvec2 block = Color_GetU2(block_offset, 0);
	uint bits_per_weight = (block.y >> 16u) & 0x3u;
	if (bits_per_weight == 0u)
		return unpackUnorm4x8(block.x).rgb;

	// In-block Vox ID & Global Bit ID
	vox_id -= (block.y >> 18u);
	uint bit_id = macro.y + (block.y & 0xFFFFu) + vox_id * bits_per_weight;

	// Read Weight Bits and Decode VBR Color
	uint weight = Color_GetWeight(weight_offset, bit_id, bits_per_weight);
	float alpha = float(weight) / float((1u << bits_per_weight) - 1u);
	return mix(Color_UnpackRGB565(block.x), Color_UnpackRGB565(block.x >> 16u), alpha);
}

vec3 Color_Fetch(in const uint root, in const uint voxel_level, in const uint leaf_level, in const uvec3 vox_pos) {
	uint ptr = root;
	vec3 rgb = vec3(0);
	[[unroll]] for (uint l = 0; l < leaf_level; ++l) {
		uint tag = ptr >> 30u, data = ptr & 0x3FFFFFFFu;
		if (tag != 0)
			return unpackUnorm4x8(data).rgb;
		uvec3 o = (vox_pos >> (voxel_level - 1u - l)) & 1u;
		ptr = uColorNodes[(ptr << 3u) | o.x | (o.y << 1u) | (o.z << 2u)];
	}
	uint tag = ptr >> 30u, data = ptr & 0x3FFFFFFFu;
	return tag == 2u ? Color_GetLeafColor(data, vox_pos & ((1u << (voxel_level - leaf_level)) - 1u))
	                 : unpackUnorm4x8(data).rgb;
}

vec3 Camera_GenRay(vec2 coord) {
	coord = coord * 2.0 - 1.0;
	return normalize(vec3(uLookX, uLookY, uLookZ) - vec3(uSideX, uSideY, uSideZ) * coord.x -
	                 vec3(uUpX, uUpY, uUpZ) * coord.y);
}

vec3 Heat(in float x) { return sin(clamp(x, 0.0, 1.0) * 3.0 - vec3(1, 2, 3)) * 0.5 + 0.5; }

void main() {
	vec2 coord = gl_FragCoord.xy / vec2(uWidth, uHeight);

	vec3 o = vec3(uPosX, uPosY, uPosZ), d = Camera_GenRay(coord);

	vec3 hit_pos, norm, vox_min, vox_max;
	uvec3 vox_pos;
	uint vox_size, iter;

	bool hit = false;
#ifdef BEAM_OPTIMIZATION
	float beam = textureLod(uBeam, coord, 0).r * 0.98;
	if (!isinf(beam)) {
		hit = DAG_RayMarch(uDAGRoot, uDAGLeafLevel, uProjectionFactor, beam, o + beam * d, d, hit_pos, norm, vox_pos,
		                   vox_size, vox_min, vox_max, iter);
	}
#else
	hit = DAG_RayMarch(uDAGRoot, uDAGLeafLevel, uProjectionFactor, 0, o, d, hit_pos, norm, vox_pos, vox_size, vox_min,
	                   vox_max, iter);
#endif

	if (uType == 0) {
		float diffuse = max(dot(norm, normalize(vec3(4, 5, 3))), 0.0) * .5 + .5;
		oColor = vec4(hit ? diffuse * Color_Fetch(uColorRoot, uVoxelLevel, uColorLeafLevel, vox_pos) : vec3(0), 1.0);
	} else if (uType == 1)
		oColor = hit ? vec4(norm * .5 + .5, 1.0) : vec4(0, 0, 0, 1);
	else
		oColor = vec4(Heat(float(iter) / 128.0), 1.0);
}

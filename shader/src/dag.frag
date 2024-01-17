#version 450
layout(std430, set = 0, binding = 0) readonly buffer uuDAG { uint uDAG[]; };

layout(location = 0) out vec4 oColor;

layout(push_constant) uniform uuPushConstant {
	float uPosX, uPosY, uPosZ, uLookX, uLookY, uLookZ, uSideX, uSideY, uSideZ, uUpX, uUpY, uUpZ;
	uint uWidth, uHeight, uDAGRoot, uDAGNodeLevels;
};

// STACK_SIZE equals to the fraction bits of float
#define STACK_SIZE 23
#define EPS 3.552713678800501e-15
struct StackItem {
	uint node;
	float t_max;
} stack[STACK_SIZE];

uint DAG_GetLeafFirstChildBits(in const uint node) {
	/* uvec2 l = uvec2(uDAG[node], uDAG[node + 1]);
	l |= l >> 1;
	l |= l >> 2;
	l |= l >> 4;
	l &= 0x01010101u; */
	uint l0 = uDAG[node], l1 = uDAG[node + 1];
	return ((l0 & 0x000000FFu) == 0u ? 0u : 0x01u) | ((l0 & 0x0000FF00u) == 0u ? 0u : 0x02u) |
	       ((l0 & 0x00FF0000u) == 0u ? 0u : 0x04u) | ((l0 & 0xFF000000u) == 0u ? 0u : 0x08u) |
	       ((l1 & 0x000000FFu) == 0u ? 0u : 0x10u) | ((l1 & 0x0000FF00u) == 0u ? 0u : 0x20u) |
	       ((l1 & 0x00FF0000u) == 0u ? 0u : 0x40u) | ((l1 & 0xFF000000u) == 0u ? 0u : 0x80u);
}

bool DAG_RayMarchLeaf(uint root, vec3 o, vec3 d, out vec3 o_pos, out vec3 o_normal, out uint o_iter) {
	if (root == -1)
		return false;

	uint iter = 0;

	d.x = abs(d.x) > EPS ? d.x : (d.x >= 0 ? EPS : -EPS);
	d.y = abs(d.y) > EPS ? d.y : (d.y >= 0 ? EPS : -EPS);
	d.z = abs(d.z) > EPS ? d.z : (d.z >= 0 ? EPS : -EPS);

	// Precompute the coefficients of tx(x), ty(y), and tz(z).
	// The octree is assumed to reside at coordinates [1, 2].
	vec3 t_coef = 1.0 / -abs(d);
	vec3 t_bias = t_coef * o;

	uint oct_mask = 0u;
	if (d.x > 0.0f)
		oct_mask ^= 1u, t_bias.x = 3.0 * t_coef.x - t_bias.x;
	if (d.y > 0.0f)
		oct_mask ^= 2u, t_bias.y = 3.0 * t_coef.y - t_bias.y;
	if (d.z > 0.0f)
		oct_mask ^= 4u, t_bias.z = 3.0 * t_coef.z - t_bias.z;

	// Initialize the active span of t-values.
	float t_min = max(max(2.0 * t_coef.x - t_bias.x, 2.0 * t_coef.y - t_bias.y), 2.0 * t_coef.z - t_bias.z);
	float t_max = min(min(t_coef.x - t_bias.x, t_coef.y - t_bias.y), t_coef.z - t_bias.z);
	t_min = max(t_min, 0.0);
	float h = t_max;

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

	const uint leaf_scale = STACK_SIZE - uDAGNodeLevels;

	while (scale < STACK_SIZE) {
		++iter;

		if (child_bits == 0u)
			child_bits =
			    scale > leaf_scale ? uDAG[parent] : (scale == leaf_scale ? DAG_GetLeafFirstChildBits(parent) : parent);
		// Determine maximum t-value of the cube by evaluating
		// tx(), ty(), and tz() at its corner.

		vec3 t_corner = pos * t_coef - t_bias;
		float tc_max = min(min(t_corner.x, t_corner.y), t_corner.z);

		uint child_shift = idx ^ oct_mask; // permute child slots based on the mirroring
		uint child_mask = 1u << child_shift;

		if ((child_bits & child_mask) != 0 && t_min <= t_max) {
			// INTERSECT
			float tv_max = min(t_max, tc_max);
			float half_scale_exp2 = scale_exp2 * 0.5;
			vec3 t_center = half_scale_exp2 * t_coef + t_corner;

			if (t_min <= tv_max) {
				if (scale < leaf_scale) // leaf node
					break;

				// PUSH
				if (tc_max < h) {
					stack[scale].node = parent;
					stack[scale].t_max = t_max;
				}
				h = tc_max;

				parent = scale > leaf_scale
				             ? uDAG[parent + 1 + bitCount(child_bits & (child_mask - 1))]
				             : (uDAG[parent + (child_shift >> 2u)] >> ((child_shift & 3u) << 3u)) & 0xFFu;

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

	vec3 t_corner = t_coef * (pos + scale_exp2) - t_bias;

	vec3 norm = (t_corner.x > t_corner.y && t_corner.x > t_corner.z)
	                ? vec3(-1, 0, 0)
	                : (t_corner.y > t_corner.z ? vec3(0, -1, 0) : vec3(0, 0, -1));
	if ((oct_mask & 1u) == 0u)
		norm.x = -norm.x;
	if ((oct_mask & 2u) == 0u)
		norm.y = -norm.y;
	if ((oct_mask & 4u) == 0u)
		norm.z = -norm.z;

	// Undo mirroring of the coordinate system.
	if ((oct_mask & 1u) != 0u)
		pos.x = 3.0 - scale_exp2 - pos.x;
	if ((oct_mask & 2u) != 0u)
		pos.y = 3.0 - scale_exp2 - pos.y;
	if ((oct_mask & 4u) != 0u)
		pos.z = 3.0 - scale_exp2 - pos.z;

	// Output results.
	o_pos = clamp(o + t_min * d, pos, pos + scale_exp2);
	if (norm.x != 0)
		o_pos.x = norm.x > 0 ? pos.x + scale_exp2 + EPS * 2 : pos.x - EPS;
	if (norm.y != 0)
		o_pos.y = norm.y > 0 ? pos.y + scale_exp2 + EPS * 2 : pos.y - EPS;
	if (norm.z != 0)
		o_pos.z = norm.z > 0 ? pos.z + scale_exp2 + EPS * 2 : pos.z - EPS;
	o_normal = norm;
	o_iter = iter;

	return scale < STACK_SIZE && t_min <= t_max;
}

vec3 Camera_GenRay() {
	vec2 coord = gl_FragCoord.xy / vec2(uWidth, uHeight);
	coord = coord * 2.0f - 1.0f;
	return normalize(vec3(uLookX, uLookY, uLookZ) - vec3(uSideX, uSideY, uSideZ) * coord.x -
	                 vec3(uUpX, uUpY, uUpZ) * coord.y);
}

vec3 Heat(in float x) { return sin(clamp(x, 0.0, 1.0) * 3.0 - vec3(1, 2, 3)) * 0.5 + 0.5; }

void main() {
	vec3 o = vec3(uPosX, uPosY, uPosZ), d = Camera_GenRay();

	vec3 pos, norm;
	uint iter;
	bool hit = DAG_RayMarchLeaf(uDAGRoot, o + 1.0, d, pos, norm, iter);

	oColor = hit ? vec4(norm * .5 + .5, 1.0) : vec4(0, 0, 0, 1);
	// oColor = vec4(Heat(float(iter) / 128.0), 1.0);
}

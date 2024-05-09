#version 460

#ifndef LENGTH
#define LENGTH 32
#endif

layout(push_constant) uniform uuPushConstant { uint uWidth, uHeight; };

void main() {
	vec2 v = vec2(0);
	v[gl_VertexIndex >> 1] = ((gl_VertexIndex & 1) << 1) - 1;
	gl_Position = vec4(vec2(v * LENGTH) / vec2(uWidth, uHeight), 0.0, 1.0);
}

#version 450
/* Copyright (c) 2019-2020, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
precision highp float;

layout(input_attachment_index = 0, binding = 0) uniform subpassInput i_depth;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput i_albedo;
layout(input_attachment_index = 2, binding = 2) uniform subpassInput i_normal;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 o_color;

layout(push_constant, std430) uniform CascadeUniform
{
	vec4 far_d;
} cascade_uniform;

layout(set = 0, binding = 3) uniform GlobalUniform
{
    mat4 inv_view_proj;
    vec2 inv_resolution;
}
global_uniform;

#include "lighting.h"

layout(set = 0, binding = 4) uniform LightsInfo
{
	Light directional_lights[MAX_LIGHT_COUNT];
	Light point_lights[MAX_LIGHT_COUNT];
	Light spot_lights[MAX_LIGHT_COUNT];
}
lights_info;

layout(constant_id = 0) const uint DIRECTIONAL_LIGHT_COUNT = 0U;
layout(constant_id = 1) const uint POINT_LIGHT_COUNT       = 0U;
layout(constant_id = 2) const uint SPOT_LIGHT_COUNT        = 0U;

layout(set = 0, binding = 5) uniform sampler2DShadow shadowmap_texture;

layout(set = 0, binding = 6) uniform ShadowUniform
{
	mat4 light_matrix;
}
shadow_uniform;

float calculate_shadow(highp vec3 pos)
{
	vec4 projected_coord = shadow_uniform.light_matrix * vec4(pos, 1.0);
	projected_coord /= projected_coord.w;
	projected_coord.xy = 0.5 * projected_coord.xy + 0.5;
	return texture(shadowmap_texture, vec3(projected_coord.xy, projected_coord.z));
}

vec3 triA = vec3(-94.0617065, 553.773804, -624.006897);
vec3 triB = vec3(-171.528931, 553.772522, -659.814880);
vec3 triC = vec3(-83.2467575, -183.583023, -626.244934);

bool pointInTriangle(vec3 p, vec3 a, vec3 b, vec3 c)
{
	vec3 v0 = c - a;
	vec3 v1 = b - a;
	vec3 v2 = p - a;

	float dot00 = dot(v0, v0);
	float dot01 = dot(v0, v1);
	float dot02 = dot(v0, v2);
	float dot11 = dot(v1, v1);
	float dot12 = dot(v1, v2);

	float inverDeno = 1.0 / (dot00 * dot11 - dot01 * dot01);

	float u = (dot11 * dot02 - dot01 * dot12) * inverDeno;
	if (u < 0 || u > 1) // if u out of range, return directly
	{
		return false;
	}

	float v = (dot00 * dot12 - dot01 * dot02) * inverDeno;
	if (v < 0 || v > 1) // if v out of range, return directly
	{
		return false;
	}

	return u + v <= 1;
}

void main()
{
	// Retrieve position from depth
	vec4  clip         = vec4(in_uv * 2.0 - 1.0, subpassLoad(i_depth).x, 1.0);

	highp vec4 world_w = global_uniform.inv_view_proj * clip;
	highp vec3 pos     = world_w.xyz / world_w.w;

//	 if (pointInTriangle(pos, triA, triB, triC)) {
//        // 在三角形内，显示白色
//        o_color = vec4(1.0, 1.0, 1.0, 1.0);
//		return;
//    }
//	else {
//		// 不在三角形内，显示黑色
//		o_color = vec4(0.0, 0.0, 0.0, 1.0);
//		return;
//	}

	vec4 albedo = subpassLoad(i_albedo);
	if(subpassLoad(i_depth).x > cascade_uniform.far_d.x)
	{
		albedo= vec4(0.8,0.2,0.3,1);
	}
	else if(subpassLoad(i_depth).x > cascade_uniform.far_d.y)
	{
		albedo= vec4(0.2,0.8,0.3,1);
	}
	else if(subpassLoad(i_depth).x > cascade_uniform.far_d.z)
	{
		albedo= vec4(0.2,0.3,0.8,1);
	}

	// Transform from [0,1] to [-1,1]
	vec3 normal = subpassLoad(i_normal).xyz;
	normal      = normalize(2.0 * normal - 1.0);
	// Calculate lighting
	vec3 L = vec3(0.0);
	for (uint i = 0U; i < DIRECTIONAL_LIGHT_COUNT; ++i)
	{
		L += apply_directional_light(lights_info.directional_lights[i], normal);
		if(i==0U)
		{
			L *= calculate_shadow(pos);
		}
	}
	for (uint i = 0U; i < POINT_LIGHT_COUNT; ++i)
	{
		L += apply_point_light(lights_info.point_lights[i], pos, normal);
	}
	for (uint i = 0U; i < SPOT_LIGHT_COUNT; ++i)
	{
		L += apply_spot_light(lights_info.spot_lights[i], pos, normal);
	}
	vec3 ambient_color = vec3(0.2) * albedo.xyz;
	
	o_color = vec4(ambient_color + L * albedo.xyz, 1.0);
}
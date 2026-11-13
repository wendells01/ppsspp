// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

static NSString *const frameBlendShader = @R"(
#include <metal_stdlib>
using namespace metal;
kernel void blend(texture2d<float, access::read> a [[texture(0)]],
                  texture2d<float, access::read> b [[texture(1)]],
                  texture2d<float, access::write> out [[texture(2)]],
                  device atomic_uint *different [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {
    if (p.x >= out.get_width() || p.y >= out.get_height()) return;
    float4 av = a.read(p), bv = b.read(p);
    out.write((av + bv) * 0.5, p);
    if (any(av != bv)) atomic_store_explicit(different, 1u, memory_order_relaxed);
    // Sparse image-wide cut detection; avoid a global atomic increment per pixel.
    if ((p.x & 15u) == 0u && (p.y & 15u) == 0u) {
        atomic_fetch_add_explicit(different + 1, 1u, memory_order_relaxed);
        if (dot(abs(av.rgb - bv.rgb), float3(1.0 / 3.0)) > 0.45)
            atomic_fetch_add_explicit(different + 2, 1u, memory_order_relaxed);
    }
}
)";

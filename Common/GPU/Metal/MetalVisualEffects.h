// Copyright (c) 2012- PPSSPP Project. Licensed under GPL-2.0-or-later.
#pragma once
#include "Common/GPU/Metal/MetalFrameTexturePool.h"
#include "Common/GPU/Metal/VisualEffectsStats.h"

// Screen-space effects on the game framebuffer, before presentation and frame
// blending. Depth is the game's stored attachment, never estimated from color.
static NSString *const visualEffectsShader = @R"(
#include <metal_stdlib>
using namespace metal;
kernel void highlights(texture2d<float, access::read> source [[texture(0)]],
                       texture2d<float, access::write> out [[texture(1)]],
                       uint2 p [[thread_position_in_grid]]) {
    if (any(p >= uint2(out.get_width(), out.get_height()))) return;
    float3 sum = 0;
    for (uint y = 0; y < 4; y++) for (uint x = 0; x < 4; x++) {
        uint2 q = min(p * 4 + uint2(x,y), uint2(source.get_width()-1, source.get_height()-1));
        float3 c = source.read(q).rgb;
        float l = dot(c, float3(0.2126,0.7152,0.0722));
        // Extract bright highlights, with a soft knee. No whole-image haze.
        sum += c * smoothstep(0.72, 1.0, l);
    }
    out.write(float4(sum / 16.0, 1), p);
}
kernel void finish(texture2d<float, access::read> source [[texture(0)]],
                   depth2d<float, access::read> depth [[texture(1)]],
                   texture2d<float, access::sample> bloom [[texture(2)]],
                   texture2d<float, access::write> out [[texture(3)]],
                   constant uint2 &flags [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {
    if (any(p >= uint2(out.get_width(), out.get_height()))) return;
    float4 original = source.read(p);
    float3 c = original.rgb;
    if (flags.y) {
        int2 size = int2(depth.get_width(), depth.get_height());
        int2 q = clamp(int2(p), int2(0), size-1);
        float z = depth.read(uint2(q));
        // Raw PSP depth is not a metric distance. Estimate the local tangent
        // plane, then count nearby samples in front of it. Flat slopes do not
        // occlude themselves. Reject sky and large depth discontinuities.
        float dx = (depth.read(uint2(clamp(q+int2(1,0),int2(0),size-1))) -
                    depth.read(uint2(clamp(q-int2(1,0),int2(0),size-1)))) * 0.5;
        float dy = (depth.read(uint2(clamp(q+int2(0,1),int2(0),size-1))) -
                    depth.read(uint2(clamp(q-int2(0,1),int2(0),size-1)))) * 0.5;
        const float2 directions[8] = {float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),
            float2(0.707,0.707),float2(-0.707,0.707),float2(0.707,-0.707),float2(-0.707,-0.707)};
        float radius = clamp(float(size.x) / 160.0, 2.0, 16.0);
        float occlusion = 0;
        for (uint i=0; i<8; i++) for (uint ring=1; ring<=2; ring++) {
            int2 offset = int2(round(directions[i] * radius * float(ring)));
            float other = depth.read(uint2(clamp(q+offset,int2(0),size-1)));
            float delta = z + dx*offset.x + dy*offset.y - other;
            float bias = max(0.00002, (abs(dx)+abs(dy))*0.5);
            float range = max(0.001, (abs(dx)+abs(dy))*radius*8.0);
            occlusion += smoothstep(bias, bias*5.0, delta) *
                (1.0-smoothstep(range,range*4.0,abs(z-other)));
        }
        if (z > 0.00001 && z < 0.99999) c *= 1.0 - 0.35 * occlusion / 16.0;
    }
    if (flags.x) {
        // Modest contrast and saturation; retain highlight headroom.
        float l = dot(c,float3(0.2126,0.7152,0.0722));
        c = mix(float3(l),c,1.06);
        c = (c-0.5)*1.035+0.5;
        constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::linear);
        float2 uv = (float2(p)+0.5)/float2(out.get_width(),out.get_height());
        float2 texel = 1.0/float2(bloom.get_width(),bloom.get_height());
        float3 glow = 0;
        float weight = 0;
        for (int y=-2; y<=2; y++) for (int x=-2; x<=2; x++) {
            float w = exp(-float(x*x+y*y)*0.5);
            glow += bloom.sample(s,uv+float2(x,y)*texel).rgb*w;
            weight += w;
        }
        c += glow / weight * 0.12;
    }
    out.write(float4(clamp(c,0.0,1.0),original.a),p);
}
)";

class MetalVisualEffects {
public:
	MetalVisualEffects(id<MTLDevice> device) : device_(device) {
		NSError *error = nil;
		id<MTLLibrary> library = [device newLibraryWithSource:visualEffectsShader options:nil error:&error];
		if (library) {
			bright_ = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"highlights"] error:&error];
			finish_ = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"finish"] error:&error];
		}
		if (!bright_ || !finish_) NSLog(@"Visual effects compilation failed: %@", error);
	}
	id<MTLTexture> Encode(id<MTLCommandBuffer> command, id<MTLTexture> source,
		id<MTLTexture> depth, bool color, bool ao) {
		if (!bright_ || !finish_ || (!color && !ao)) return nil;
		// Unsupported/mismatched depth is an explicit AO bypass.
		ao = ao && depth && depth.textureType == MTLTextureType2D &&
			depth.width == source.width && depth.height == source.height;
		if (!color && !ao) return nil;
		auto outputLease = outputPool_.Acquire(device_, source.width, source.height, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
		auto bloomLease = bloomPool_.Acquire(device_, (source.width+3)/4, (source.height+3)/4, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
		if (!outputLease || !bloomLease) return nil;
		id<MTLTexture> output = outputLease->texture;
		id<MTLTexture> bloom = bloomLease->texture;
		id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
		encoder.label = @"GT color, selective bloom and depth AO";
		if (color) {
			[encoder setComputePipelineState:bright_];
			[encoder setTexture:source atIndex:0];
			[encoder setTexture:bloom atIndex:1];
			[encoder dispatchThreads:MTLSizeMake(bloom.width,bloom.height,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
		}
		[encoder setComputePipelineState:finish_];
		[encoder setTexture:source atIndex:0];
		[encoder setTexture:depth atIndex:1];
		[encoder setTexture:bloom atIndex:2];
		[encoder setTexture:output atIndex:3];
		uint32_t flags[2] = {color, ao};
		[encoder setBytes:flags length:sizeof(flags) atIndex:0];
		[encoder dispatchThreads:MTLSizeMake(source.width,source.height,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
		[encoder endEncoding];
		// Replay uses unretained command buffers. Retain all temporary resources
		// through GPU completion, including the depth attachment.
		[command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
			(void)source; (void)depth; (void)outputLease; (void)bloomLease;
			if (completed.status == MTLCommandBufferStatusCompleted) {
				if (color) VisualEffects::colorFrames++;
				if (ao) VisualEffects::aoFrames++;
			}
		}];
		return output;
	}
private:
	id<MTLDevice> device_;
	id<MTLComputePipelineState> bright_, finish_;
	MetalFrameTexturePool outputPool_, bloomPool_;
};

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

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "Common/GPU/Metal/MetalFrameTexturePool.h"
#include "Common/GPU/Metal/MetalFrameBlendShader.h"

int main() {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (!device) { puts("SKIP: runner has no Metal device"); return 77; }
		MetalFrameTexturePool pool;
		auto first = pool.Acquire(device, 2, 1, MTLTextureUsageShaderRead);
		assert(first);
		auto leasedTexture = first->texture;
		auto second = pool.Acquire(device, 2, 1, MTLTextureUsageShaderRead);
		assert(second && second->texture != leasedTexture);
		first.reset();
		auto reused = pool.Acquire(device, 2, 1, MTLTextureUsageShaderRead);
		assert(reused->texture == leasedTexture);
		puts("PASS: texture pool preserves active leases and reuses released slots");
		NSError *error = nil;
		id<MTLLibrary> library = [device newLibraryWithSource:frameBlendShader options:nil error:&error];
		if (!library) { fprintf(stderr, "%s\n", error.localizedDescription.UTF8String); return 1; }
		id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"blend"] error:&error];
		assert(pipeline);
		MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:2 height:1 mipmapped:NO];
		desc.storageMode = MTLStorageModeShared;
		desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		id<MTLTexture> a = [device newTextureWithDescriptor:desc], b = [device newTextureWithDescriptor:desc], out = [device newTextureWithDescriptor:desc];
		uint8_t av[8] = {0,0,0,255,255,0,0,255}, bv[8] = {255,255,255,255,0,0,255,255};
		[a replaceRegion:MTLRegionMake2D(0,0,2,1) mipmapLevel:0 withBytes:av bytesPerRow:8];
		[b replaceRegion:MTLRegionMake2D(0,0,2,1) mipmapLevel:0 withBytes:bv bytesPerRow:8];
		id<MTLBuffer> different = [device newBufferWithLength:12 options:MTLResourceStorageModeShared];
		id<MTLCommandQueue> queue = [device newCommandQueue];
		for (int same = 0; same < 3; ++same) {
			if (same == 2) {
				for (int i = 0; i < 8; ++i) bv[i] = i % 4 == 3 ? 255 : 64;
				[b replaceRegion:MTLRegionMake2D(0,0,2,1) mipmapLevel:0 withBytes:bv bytesPerRow:8];
			}
			memset(different.contents, 0, 12);
			id<MTLCommandBuffer> cmd = [queue commandBuffer];
			id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
			[enc setComputePipelineState:pipeline];
			[enc setTexture:a atIndex:0]; [enc setTexture:same == 1 ? a : b atIndex:1]; [enc setTexture:out atIndex:2];
			[enc setBuffer:different offset:0 atIndex:0];
			[enc dispatchThreads:MTLSizeMake(2,1,1) threadsPerThreadgroup:MTLSizeMake(2,1,1)];
			[enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
			assert(cmd.status == MTLCommandBufferStatusCompleted);
			assert(*(uint32_t *)different.contents == (same == 1 ? 0u : 1u));
			assert(((uint32_t *)different.contents)[1] == 1);
			assert(((uint32_t *)different.contents)[2] == (same == 0 ? 1u : 0u));
			uint8_t pixels[8];
			[out getBytes:pixels bytesPerRow:8 fromRegion:MTLRegionMake2D(0,0,2,1) mipmapLevel:0];
			for (int i = 0; i < 8; ++i) {
				int expected = same == 1 ? av[i] : (av[i] + bv[i]) / 2;
				assert(abs((int)pixels[i] - expected) <= 1);
			}
		}
		puts("PASS: GPU midpoint differs from both originals; identical pair rejected; hard cut and gradual change distinguished");
	}
}

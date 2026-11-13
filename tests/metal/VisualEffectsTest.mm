// Copyright (c) 2012- PPSSPP Project. Licensed under GPL-2.0-or-later.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include "Common/GPU/Metal/MetalVisualEffects.h"

int main() {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (!device) { puts("SKIP: no Metal device"); return 77; }
		id<MTLCommandQueue> queue = [device newCommandQueue];
		MetalVisualEffects effects(device);
		constexpr int width = 256, height = 128;
		MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width height:height mipmapped:NO];
		desc.storageMode = MTLStorageModeShared;
		desc.usage = MTLTextureUsageShaderRead;
		id<MTLTexture> source = [device newTextureWithDescriptor:desc];
		desc.pixelFormat = MTLPixelFormatDepth32Float;
		desc.storageMode = MTLStorageModePrivate;
		desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
		id<MTLTexture> depth = [device newTextureWithDescriptor:desc];
		assert(source && depth);
		std::vector<uint8_t> pixels(width*height*4,128);
		for (int i=0; i<width*height; i++) pixels[i*4+3]=255;
		[source replaceRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:width*4];
		for (int test=0; test<4; test++) {
			std::vector<float> z(width*height,0.5f);
			if (test==1) for (int y=40; y<88; y++) for (int x=80; x<176; x++) z[y*width+x]=0.4997f;
			if (test==2) {
				for (int i=0; i<width*height; i++) for (int c=0;c<3;c++) pixels[i*4+c]=0;
				for (int y=40;y<88;y++) for (int x=80;x<176;x++) for (int c=0;c<3;c++) pixels[(y*width+x)*4+c]=255;
				[source replaceRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:width*4];
			}
			id<MTLBuffer> upload = [device newBufferWithBytes:z.data() length:z.size()*4 options:MTLResourceStorageModeShared];
			id<MTLCommandBuffer> command = [queue commandBuffer];
			id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
			[blit copyFromBuffer:upload sourceOffset:0 sourceBytesPerRow:width*4 sourceBytesPerImage:width*height*4 sourceSize:MTLSizeMake(width,height,1)
				toTexture:depth destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
			[blit endEncoding];
			id<MTLTexture> out = effects.Encode(command,source,depth,test==2,test<2);
			if (test==3) { assert(!out); continue; }
			assert(out && out.width==width && out.height==height);
			id<MTLBuffer> readback = [device newBufferWithLength:width*height*4 options:MTLResourceStorageModeShared];
			blit = [command blitCommandEncoder];
			[blit copyFromTexture:out sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(width,height,1)
				toBuffer:readback destinationOffset:0 destinationBytesPerRow:width*4 destinationBytesPerImage:width*height*4];
			[blit endEncoding]; [command commit]; [command waitUntilCompleted];
			if (command.status != MTLCommandBufferStatusCompleted) { NSLog(@"%@",command.error); return 1; }
			const uint8_t *values = (const uint8_t *)readback.contents;
			int darkened=0;
			for (int i=0;i<width*height;i++) {
				assert(values[i*4+3]==255);
				if (test==0) assert(values[i*4]==128);
				if (test==1 && values[i*4]<127) darkened++;
			}
			if (test==1) assert(darkened>0);
			if (test==2) {
				assert(values[(64*width+76)*4]>0); // Bloom spills beyond a bright source.
				assert(values[0]==0); // Distant black remains black.
			}
		}
		puts("PASS: full resolution retained; flat depth unchanged; depth-only occlusion; selective bloom; OFF bypass");
	}
}

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

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "Common/GPU/Metal/FrameGenerationStats.h"
#include "Common/GPU/Metal/MetalFrameBlendShader.h"

// Only included by MetalRenderManager.mm. All texture processing stays on the GPU.
// Own queue/thread: a stalled drawable never waits on UIKit or the emulator thread.
class MetalFrameGeneration {
public:
	struct Frame {
		id<MTLTexture> texture;
		double time;
	};
	MetalFrameGeneration(id<MTLDevice> device, CAMetalLayer *layer) : device_(device), layer_(layer) {
		queue_ = [device newCommandQueue];
		NSError *error = nil;
		id<MTLLibrary> lib = [device newLibraryWithSource:frameBlendShader options:nil error:&error];
		pipeline_ = [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"blend"] error:&error];
		if (!pipeline_) {
			ERROR_LOG(Log::G3D, "Frame generation shader: %s", error.localizedDescription.UTF8String);
			return;
		}
		thread_ = std::thread([this]() { Run(); });
	}
	~MetalFrameGeneration() {
		{
			std::lock_guard<std::mutex> guard(mutex_);
			stop_ = true;
		}
		cv_.notify_all();
		if (thread_.joinable()) thread_.join();
	}
	bool Ready() const { return pipeline_ != nil; }
	void Push(id<MTLTexture> texture, double time) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (frames_.size() == 3) {
			frames_.pop_front();
			FrameGeneration::dropped++;
		}
		frames_.push_back({texture, time});
		cv_.notify_one();
	}
private:
	void Present(id<MTLTexture> texture) {
		// Explicit display deadlines, independent of emulation/VBlank. Never catch up in a burst.
		double now = CACurrentMediaTime();
		deadline_ = std::max(deadline_ + 1.0 / 120.0, now + 1.0 / 120.0);
		std::this_thread::sleep_for(std::chrono::duration<double>(std::max(0.0, deadline_ - now - 0.002)));
		id<CAMetalDrawable> drawable = [layer_ nextDrawable];
		if (!drawable) return;
		if (drawable.texture.width != texture.width || drawable.texture.height != texture.height) return;
		id<MTLCommandBuffer> cmd = [queue_ commandBuffer];
		id<MTLBlitCommandEncoder> enc = [cmd blitCommandEncoder];
		[enc copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
			sourceSize:MTLSizeMake(texture.width, texture.height, 1) toTexture:drawable.texture
			destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
		[enc endEncoding];
		[drawable addPresentedHandler:^(id<MTLDrawable> d) {
			if (d.presentedTime > 0.0) FrameGeneration::presented++;
		}];
		[cmd presentDrawable:drawable atTime:deadline_];
		[cmd commit];
	}
	void Run() {
		Frame previous{nil, 0};
		for (;;) {
			@autoreleasepool {
				Frame current;
				{
					std::unique_lock<std::mutex> guard(mutex_);
					cv_.wait(guard, [this]() { return stop_ || !frames_.empty(); });
					if (stop_) break;
					current = frames_.front();
					frames_.pop_front();
				}
				double interval = current.time - previous.time;
				if (previous.texture && interval > 0.010 && interval < 0.022 &&
					previous.texture.width == current.texture.width && previous.texture.height == current.texture.height) {
					MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:current.texture.pixelFormat
						width:current.texture.width height:current.texture.height mipmapped:NO];
					desc.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
					desc.storageMode = MTLStorageModePrivate;
					id<MTLTexture> mixed = [device_ newTextureWithDescriptor:desc];
					id<MTLBuffer> different = [device_ newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
					*(uint32_t *)different.contents = 0;
					id<MTLCommandBuffer> cmd = [queue_ commandBuffer];
					id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
					[enc setComputePipelineState:pipeline_];
					[enc setTexture:previous.texture atIndex:0];
					[enc setTexture:current.texture atIndex:1];
					[enc setTexture:mixed atIndex:2];
					[enc setBuffer:different offset:0 atIndex:0];
					[enc dispatchThreads:MTLSizeMake(desc.width, desc.height, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
					[enc endEncoding];
					[cmd commit];
					// Only this worker waits; the CPU sees one flag, never image pixels.
					[cmd waitUntilCompleted];
					if (cmd.status == MTLCommandBufferStatusCompleted) {
						FrameGeneration::shaderNS += (uint64_t)((cmd.GPUEndTime - cmd.GPUStartTime) * 1e9);
						FrameGeneration::shaderSamples++;
					}
					if (cmd.status == MTLCommandBufferStatusCompleted && *(uint32_t *)different.contents) {
						FrameGeneration::generated++;
						Present(mixed);
					}
				}
				Present(current.texture);
				previous = current;
			}
		}
		// Drain retained command buffers before releasing the queue and layer.
		id<MTLCommandBuffer> drain = [queue_ commandBuffer];
		[drain commit];
		[drain waitUntilCompleted];
	}
	id<MTLDevice> device_;
	CAMetalLayer *layer_;
	id<MTLCommandQueue> queue_;
	id<MTLComputePipelineState> pipeline_;
	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<Frame> frames_;
	bool stop_ = false;
	double deadline_ = 0;
};

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
#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

struct MetalFrameTexture {
	id<MTLTexture> texture;
};

// A lease must survive every GPU command using the texture. No waits on acquisition.
class MetalFrameTexturePool {
public:
	std::shared_ptr<MetalFrameTexture> Acquire(id<MTLDevice> device, NSUInteger width, NSUInteger height, MTLTextureUsage usage) {
		std::lock_guard<std::mutex> guard(mutex_);
		slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [&](const auto &slot) {
			return slot.use_count() == 1 && (slot->texture.width != width || slot->texture.height != height);
		}), slots_.end());
		for (const auto &slot : slots_) {
			if (slot.use_count() == 1) return slot;
		}
		if (slots_.size() >= 12) return {};
		MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
		desc.usage = usage;
		desc.storageMode = MTLStorageModePrivate;
		auto slot = std::make_shared<MetalFrameTexture>();
		slot->texture = [device newTextureWithDescriptor:desc];
		if (!slot->texture) return {};
		slots_.push_back(slot);
		return slot;
	}
private:
	std::mutex mutex_;
	std::vector<std::shared_ptr<MetalFrameTexture>> slots_;
};

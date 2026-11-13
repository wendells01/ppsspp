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
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>

// Cumulative measured events. Read by the overlay, written by GPU/presentation callbacks.
namespace FrameGeneration {
inline std::atomic<uint64_t> real{0}, generated{0}, presented{0}, shaderNS{0}, shaderSamples{0}, dropped{0};
inline std::atomic<uint64_t> displayTicks{0}, displayIntervalNS{0};
inline std::atomic<int> requestedHz{0};
inline std::mutex displayMutex;
inline std::condition_variable displayCV;

// Called on UIKit's display link. Never acquire a drawable or wait for GPU work here.
inline void DisplayTick(double interval) {
	if (interval > 0.0) displayIntervalNS = (uint64_t)(interval * 1e9);
	{
		std::lock_guard<std::mutex> guard(displayMutex);
		displayTicks++;
	}
	displayCV.notify_all();
}
}

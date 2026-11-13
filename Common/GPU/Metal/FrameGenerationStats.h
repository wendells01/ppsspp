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
#include <algorithm>
#include <array>
#include <vector>

// Cumulative measured events. Read by the overlay, written by GPU/presentation callbacks.
namespace FrameGeneration {
inline std::atomic<uint64_t> real{0}, generated{0}, presented{0}, shaderNS{0}, shaderSamples{0}, dropped{0};
inline std::atomic<uint64_t> displayTicks{0}, displayIntervalNS{0};
inline std::atomic<int> requestedHz{0}, thermalState{0};
inline std::atomic<bool> lowPower{false};
inline std::atomic<uint64_t> sceneCuts{0}, latePresentations{0};
struct Metrics {
	double intervalP50 = 0, intervalP95 = 0, intervalP99 = 0;
	double latencyP50 = 0, latencyP95 = 0, latencyP99 = 0;
	uint64_t samples = 0;
};
inline std::mutex metricsMutex;
inline std::array<double, 1024> intervals{}, latencies{};
inline size_t intervalCount = 0, latencyCount = 0;
inline double lastPresentedTime = 0;

inline void RecordPresentation(double time, double sourceTime) {
	std::lock_guard<std::mutex> guard(metricsMutex);
	if (time > lastPresentedTime) {
		double interval = time - lastPresentedTime;
		if (lastPresentedTime > 0 && interval < 0.25) {
			intervals[intervalCount++ % intervals.size()] = interval * 1000;
			double expected = displayIntervalNS.load() / 1e9;
			if (expected > 0 && interval > expected * 1.5) latePresentations++;
		}
		lastPresentedTime = time;
	}
	if (time >= sourceTime) latencies[latencyCount++ % latencies.size()] = (time - sourceTime) * 1000;
}

inline Metrics ReadMetrics() {
	std::vector<double> frames, latency;
	{
		std::lock_guard<std::mutex> guard(metricsMutex);
		frames.assign(intervals.begin(), intervals.begin() + std::min(intervalCount, intervals.size()));
		latency.assign(latencies.begin(), latencies.begin() + std::min(latencyCount, latencies.size()));
	}
	std::sort(frames.begin(), frames.end());
	std::sort(latency.begin(), latency.end());
	auto percentile = [](const std::vector<double> &v, double q) {
		return v.empty() ? 0.0 : v[(size_t)((v.size() - 1) * q)];
	};
	return {percentile(frames, .5), percentile(frames, .95), percentile(frames, .99),
		percentile(latency, .5), percentile(latency, .95), percentile(latency, .99), (uint64_t)frames.size()};
}
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

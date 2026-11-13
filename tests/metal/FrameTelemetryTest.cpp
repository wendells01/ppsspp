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

#include <cassert>
#include <cmath>
#include <cstdio>
#include "Common/GPU/Metal/FrameGenerationStats.h"
int main() {
	using namespace FrameGeneration;
	displayIntervalNS = 8333333;
	for (int i = 0; i <= 120; i++) {
		double time = 10.0 + i / 120.0;
		RecordPresentation(time, time - .025);
	}
	auto metrics = ReadMetrics();
	assert(metrics.samples == 120);
	assert(std::abs(metrics.intervalP95 - 1000.0 / 120) < .001);
	assert(std::abs(metrics.latencyP95 - 25) < .001);
	assert(latePresentations.load() == 0);
	RecordPresentation(11.0 + 1.0 / 60, 11.0);
	assert(latePresentations.load() == 1);
	puts("PASS: presentation percentiles, source-to-display latency and late-frame detection");
}

// Copyright (c) 2012- PPSSPP Project. Licensed under GPL-2.0-or-later.
#pragma once
#include <atomic>
#include <cstdint>
namespace VisualEffects {
inline std::atomic<uint64_t> colorFrames{0}, aoFrames{0};
}

# Experimental iOS Metal frame generation

Base: upstream master 1aebad2 (development tree, not a 1.21 release).

Graphics > Rendering > Experimental Frame Generation (default OFF).
Only iOS Metal in gameplay uses the new presentation path. Menus and OFF use
upstream drawable presentation. No PSP clock, CPU, VBlank, cheat or game patch changes.

## Pipeline

The render manager redirects the final backbuffer (including overlays) to a private
BGRA8 texture. After GPU completion, new PSP flip serials enter a bounded queue
(three waiting textures, oldest dropped on overload). Completed textures cross
command queues only after their producer has finished; resources are retained
through command completion. The presenter owns its own worker and Metal queue.

The worker presents A, waits for B, computes (A+B)/2, then presents AB and B.
There is lookahead latency, plus GPU and drawable queue latency. The worker submits at most one drawable per CADisplayLink callback;
callbacks do not drive emulation. Future timed presents and synthetic sleep
deadlines are not used. A main-run-loop
CADisplayLink requests the ProMotion range; it does no rendering. The plist removes
the phone's default minimum-frame-duration restriction. Actual refresh remains
under iOS control (power, temperature, external display, GPU load).

A compute shader also sets a four-byte difference flag. Identical images never
increment generated FPS. No CPU image readback occurs. Only adjacent source serials with a smoothed input period of
12–22 ms and display callbacks above 83 Hz are eligible: a steady 30 FPS stream gets no generated frames. This is a
conservative initial gate, not a claim of 120 FPS from slow games. The midpoint
is simple blending in the existing unorm color space, with expected ghosting.

The overlay samples cumulative events once a second:

- real: completed captured frames with new PSP flip serials; not the monitor rate;
- generated: successful GPU midpoint computations for nonidentical images;
- presented: drawable presented handlers with positive presentedTime;
- GPU blend ms: average GPU command duration for blend/compare dispatches,
  including command overhead, excluding presentation and CPU wait.

Generated and presented are deliberately independent: computed output can miss a
deadline or fail to acquire a drawable. Real FPS counts renders, not a proof that
every render contains new motion. Multiple displays aggregate counters. The
counters and UI are themselves included in captured images.

## Reproducible iOS build

On a fork containing this branch, run the **iOS Experimental Frame Generation**
workflow manually. It checks out recursive submodules, compiles a standalone Metal
kernel test, builds an unsigned arm64 device archive with Xcode, and packages
`PPSSPP-FrameGeneration-unsigned.ipa` as an artifact. No developer certificate is
needed for packaging. SideStore/AltStore must sign the app for installation.
The standard sideload build is used; JIT availability still depends on the device
and sideload setup. No private-entitlement ldid signing or jailbreak is required
by this workflow. The workflow successfully built the device archive on 2026-11-13 using Xcode 16.4 (iOS SDK 18.5).

Equivalent macOS build commands are the Configure/Build/Package steps in
`.github/workflows/ios-frame-generation.yml`. The standalone GPU test uses two
known RGBA images and verifies midpoint pixels and identical-pair rejection. A
hosted runner without a Metal device reports a skip, not a GPU-test success.

## Minimum device validation

1. Install the signed artifact on iPhone 16 Pro Max. Launch with the option OFF;
   verify menus, game launch, audio, rotation and background/foreground operation.
2. Run a legally supplied Gran Turismo UCES01245 at normal speed, with existing
   120-FPS timing cheats disabled, frameskip off and normal speed limit. Establish
   the OFF baseline and confirm JIT status. Prefer a race delivering ~60 real FPS.
3. Enable the option. Check real remains near its baseline, generated becomes
   nonzero and presented rises. Inspect 240-FPS camera footage or a Metal capture
   to verify A/AB/B order, midpoint imagery and actual display cadence. Counters
   alone do not prove 120 distinct display scanouts or correct physics.
4. Compare race timer against wall clock and listen for audio drift. Inspect GPU
   blend time. Test a 30-FPS scene: generated should settle at zero. Disable the
   option, rotate, pause/resume, background/foreground, exit and relaunch the game.

## Validation boundary

Linux cannot compile Apple Metal/UIKit code or produce the iOS binary without an
Apple SDK/toolchain. A Linux build checks the shared configuration and overlay
only. iPhone startup, actual ProMotion cadence, thermal behavior, physics, and
sideload installation remain unverified until the macOS build and device steps
are completed. This prototype has no optical flow or motion estimation.

## Recorded build and delivery

- Source commit: `bdc355b`; fork: https://github.com/wendells01/ppsspp
- Successful run: https://github.com/wendells01/ppsspp/actions/runs/43422503076
- IPA artifact: https://github.com/wendells01/ppsspp/actions/runs/43422503076/artifacts/14034738905
- IPA: 20,017,367 bytes; SHA256 `9d92e7f5164443dbb610f7a864d56f9cfefcaec3a8a52b66abf143976f48e274`.
- Archive succeeded. Standalone GPU test actually ran and passed on the macOS
  runner (not skipped): expected midpoint pixels and identical-pair rejection.
  This tests synthetic textures, not an emulated game or iPhone display cadence.
- Local package inspection: valid ZIP, iOS arm64 Mach-O, minimum iOS 15,
  ProMotion plist key true, embedded blend shader and experimental UI label,
  unencrypted executable, only system dylib dependencies. Unsigned by design.
- Taildrop transfer to the online `16Pro` peer completed successfully.
- Linux full headless/shared UI build completed; all 102 C++ unit tests passed.
  Focused C++ tests ConfigSettings,
  Parsers, UITabOrder, UIFocusNavigation and UIFlowLayout passed (5/5).
- Focused PSP display tests: display, setframebuf, vblankmulti and vblankphase
  passed; vblanklen failed due to expected timing-range differences (4/5).
  No baseline comparison was performed, so this is not classified as a regression
  or pre-existing failure. No emulator timing code was changed.
- The broad PSP suite was also invoked and is not green (its main group reported
  501 passed, 93 failed, 2 missing; additional network tests have failures).
  Those results do not validate this iOS presentation path. Broader emulation
  repairs are outside the prototype's scope.

Installation, normal OFF behavior on device, real game intermediate images,
physics/audio pacing, and 120-Hz scanout remain untested on the iPhone.

## Presentation correction (second build)

Device report on the first build: real 59.9, generated 19.7, presented 59.9,
GPU blend 0.912 ms. Enabling the upstream duplicate-to-60-Hz option reportedly
raised presented to around 100. This does not prove a fixed 60-Hz system cap.
Live debugger confirmed JIT and normal emulation speed (ratio 1.004).

The second build replaces sleep/future-time presentation with a display-link
condition signal and untimed presentation on the worker. The rate request is
120/120/120, only changed when necessary. It reports actual callback frequency
and queue drops. At 60-Hz callbacks it skips intermediates to preserve real
frames. It also smooths source arrival cadence before queue drops, avoiding
rejecting a 60-FPS stream merely because host submissions arrive irregularly.
Serial adjacency and GPU image difference checks remain mandatory.

`gpu.stats.get` now includes a `frameGeneration` object: enabled, real,
generated, presented, dropped, shaderNS, shaderSamples, displayTicks,
displayIntervalNS, requestedHz. Counters are cumulative; use successive deltas
and elapsed wall time. Display-link callbacks are not proof of distinct physical
scanouts. The corrected cadence still requires another test on the iPhone.

Second build result: source `0072798`, successful Xcode archive and actual GPU
midpoint/identical-pair test (BGRA8) in run
https://github.com/wendells01/ppsspp/actions/runs/43426780093 .
Artifact: https://github.com/wendells01/ppsspp/actions/runs/43426780093/artifacts/14037713522 .
Delivered file: `PPSSPP-FrameGeneration-v2-0072798.ipa`, 20,019,819 bytes,
SHA256 `6589103c59826012567c1883f2b6188a12f0cb580175ce97ae3ae96108fc03da`.
Local inspection confirmed arm64, the ProMotion key, source revision, new overlay
labels and debugger fields. The new `gpu.stats.get` fields were also verified
through a live local debugger connection. Improved iPhone cadence remains pending
reinstallation and another device measurement.

## Third build: bounded latency and resource reuse

The capture and midpoint texture pools retain leases through GPU completion;
slots cannot be reused while a command buffer or pending/previous frame owns
them. Each pool is bounded at 12 slots and discards free old-size textures on
resize. The shared three-word comparison buffer is reused after compute completion.
The pending queue is limited to two frames; out-of-order arrivals and frames
older than 40 ms are discarded rather than accumulating presentation delay.

The GPU sparsely counts large color changes. If over 85 percent of sampled
pixels differ by more than 0.45 mean RGB, the candidate is treated as a hard cut
and only the real image is presented. This is deliberately conservative and
cannot remove ordinary frame-blending ghosting.

Actual drawable presentedTime callbacks now populate rolling 1024-sample frame
interval and source-to-display latency p50/p95/p99 measurements in milliseconds.
This latency starts at final-frame submission; it does not measure full controller
input latency. Gaps above 250 ms are excluded from frame-interval percentiles.
The overlay shows p95 interval/latency and cut count. The debugger additionally
reports late presentations, thermal state and Low Power Mode. Serious/critical
thermal state or Low Power Mode suspends the 120-Hz request and interpolation.

Regression risks: a conservative cut heuristic may skip a useful midpoint;
the reduced queue can drop frames under overload; incorrect GPU resource leases
would corrupt images. Mac GPU tests cover pooling and cut statistics, while the
new presentation-metrics test checks percentiles and late-frame accounting.
Long-session heat/battery and foreground/background behavior still require this
build installed on the phone; no synthetic test establishes those results.

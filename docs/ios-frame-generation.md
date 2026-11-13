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
There is lookahead latency, plus GPU and drawable queue latency. Deadlines advance
at 1/120 second and reset after stalls; they do not drive emulation. A main-run-loop
CADisplayLink requests the ProMotion range; it does no rendering. The plist removes
the phone's default minimum-frame-duration restriction. Actual refresh remains
under iOS control (power, temperature, external display, GPU load).

A compute shader also sets a four-byte difference flag. Identical images never
increment generated FPS. No CPU image readback occurs. Only source intervals of
10–22 ms are eligible: a steady 30 FPS stream gets no generated frames. This is a
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

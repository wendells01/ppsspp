# V3 with experimental GT visual effects

This build extends the existing iOS V3 frame-generation branch. It has no neural
runtime or model dependencies. Frame generation, color/bloom, and depth ambient
occlusion have independent switches under Graphics > Rendering. New effects
default to OFF so an existing installation retains its baseline appearance.

Enable **Experimental GT Color + Selective Bloom** for a modest contrast and
saturation adjustment plus a soft highlight bloom. Highlights are extracted at
quarter resolution; the final image retains the framebuffer's original size.
This is a screen-space appearance adjustment, not new physical lighting or HDR.

Enable **Experimental Depth Ambient Occlusion** for depth-based obscurance.
The renderer pairs each game color framebuffer with its stored depth attachment.
Before that color is sampled by a presentation pass, a compute pass estimates a
local depth tangent and compares nearby depth samples. Flat surfaces stay flat;
nearby depth discontinuities can darken contact regions. Large discontinuities
are attenuated to limit halos. Strength is deliberately conservative.

PSP depth is nonlinear and game dependent. This prototype does not recover the
camera projection, world-space normals, hidden geometry, or material information.
It can miss contact shadows, darken edges incorrectly, or be ineffective if the
game clears/overwrites depth before presentation. It is not ray-traced lighting.
Depth must be sampleable and match the source color's size; otherwise AO is
bypassed. It never estimates depth from image brightness.

The `FX completed: color ... | depth AO ...` overlay counts effects whose GPU
command buffer completed successfully. Zero while a switch is enabled means the
presentation input was not matched to a compatible game framebuffer, or GPU work
failed. A rising AO counter establishes execution with a depth attachment; it
does not establish that the game retained useful depth or that the result looks
correct. The existing real/generated/presented FPS counters remain separate.

UI atlas draws are untouched. HUD graphics already rendered inside the game
framebuffer can still be affected. Existing postprocessing shaders can insert an
intermediate framebuffer without a paired depth attachment. For the first GT
test, disable other postprocessing shaders; HD texture replacement can stay on.

Temporary textures use bounded V3 texture-pool leases retained through GPU
completion. All computation stays on the GPU. The effects run on real rendered
frames before final presentation and frame blending; they do not run the PSP CPU
or game logic again and do not change the game's VBlank rate.

## Validation

The iOS workflow compiles and executes `VisualEffectsTest.mm` on a Metal device
when available. It checks full-size output, unchanged flat-depth AO, occlusion
caused by depth alone, highlight bloom outside a bright patch, and OFF bypass.
The existing frame-blending and telemetry tests also run. A runner without a
Metal device explicitly reports skipped GPU execution.

On iPhone, compare the same paused GT scene with both effects OFF, color only,
AO only, and both ON. Check cars/roadside geometry for useful contact darkening,
white HUD graphics for excessive bloom, and the existing real/presented FPS for
performance loss. Native compilation and synthetic GPU tests do not establish
visual quality or sustained 120 Hz gameplay on the physical phone.

Build `4bcf450` compiled and archived for iPhoneOS in
[Actions run 43445092554](https://github.com/wendells01/ppsspp/actions/runs/43445092554).
The presentation telemetry, original Metal blending/pool tests, and visual-effects
GPU tests all executed successfully; none of the GPU tests was skipped. The IPA
ZIP was checked, its identifier matched V3 (`org.ppsspp.ppsspp`), and the ProMotion
Info.plist flag remained enabled. iPhone visual/performance validation is pending.

The separate neural-rendering experiment was removed at the user's request:
local upstream clones, DLL/model downloads, conversion environment, generated
weights, probe IPAs, and NR diagnostics were deleted. Its two experimental GitHub
branches and five Actions runs/artifacts were deleted. The V3 branch, previous
V3 IPAs, and frame-generation diagnostics were preserved. Copies already received
on the phone require deletion on the phone.

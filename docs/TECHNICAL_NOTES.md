# Engineering Notes

This project is meant to show low-level graphics ownership: resource lifetime,
GPU synchronization, shader data layout, real-time simulation, and enough UI to
inspect the renderer while it is running.

## What I Optimized For

**Keep the simulation on the GPU.** The CPU only rebuilds deterministic
Tessendorf spectrum seeds when the spectral basis changes. Per-frame spectrum
evolution, FFT passes, displacement, normals, derivatives, and foam signals stay
GPU-resident, which avoids texture uploads on the hot path.

**Protect shader-facing ABI.** CPU structs that map to shader uniforms and push
constants have explicit layout assertions. This catches accidental padding or
offset changes at compile time instead of turning them into visual artifacts.

**Prefer deterministic rebuild points.** Ocean resources are recreated only
when resolution or spectral seed revision changes. Parameters such as height
scale and choppiness can update frame-to-frame without rebuilding the spectrum.

**Favor direct instrumentation.** The renderer exposes CPU frame history,
per-scope profiler samples, GPU FFT time, and scene/UI GPU time in ImGui. That
makes performance tradeoffs visible while tuning simulation and shading
parameters.

## Tradeoffs I Would Revisit

**Frames in flight.** The renderer currently keeps the frame loop simple with a
single frame fence. Moving to two or three frames in flight would improve GPU
occupancy, but it would also require per-frame uniform buffers, command-buffer
ownership, and more careful timestamp bookkeeping.

**Transfer strategy.** Mesh and seed data are uploaded through host-visible
memory because they change infrequently and the code path is easy to inspect.
For larger scenes, I would move static mesh data through staging buffers into
device-local memory.

**Compute scheduling.** Ocean compute runs before graphics in the same command
buffer. That keeps hazards explicit and predictable. A future version could
explore async compute, but only after measuring whether overlap beats the added
queue synchronization complexity on the target hardware.

**Pipeline startup cost.** Pipelines are built directly at startup and rebuilt
where needed during swapchain recreation. A production renderer should add a
pipeline cache and consider precompiled pipeline variants for faster startup and
iteration.

**Render validation.** The current project relies on build checks, validation
layers, profiler output, and manual visual inspection.

**Water model scope.** The optical model is tuned for plausible real-time ocean
rendering rather than physically exhaustive water transport. The next quality
step would be better energy calibration across sky, sun glints, underwater
attenuation, and foam response.

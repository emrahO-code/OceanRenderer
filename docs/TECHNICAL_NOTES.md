# Technical Decisions and Known Limitations

## Technical Decisions

- **Vulkan-only rendering path.** GLFW creates a no-API window and all rendering
  goes through Vulkan. This keeps the frame model explicit and avoids a mixed
  OpenGL/Vulkan backend.
- **Private renderer implementation.** `VulkanRenderer` keeps a small stable
  public API while `VulkanRenderer::Impl` owns Vulkan handles and module-local
  implementation details.
- **GPU-resident ocean simulation.** The CPU generates compact deterministic
  spectral seeds only when basis settings change. Per-frame spectrum evolution,
  inverse FFT, displacement, normal, derivative, and foam data are computed on
  the GPU.
- **Single graphics/compute queue path.** The renderer selects a queue family
  that supports graphics and uses it for compute dispatch and scene rendering.
  This avoids cross-queue synchronization complexity.
- **Swapchain-sized frame resources.** Command buffers and render-finished
  semaphores follow the swapchain image count and are rebuilt during swapchain
  recreation.
- **Host-visible mesh buffers.** LOD mesh vertex and index buffers are filled
  directly from host-visible memory. This is simple and acceptable because mesh
  data changes only when ocean resources are recreated.
- **Shader ABI assertions.** CPU uniform and push-constant structs use explicit
  layout assertions to protect shader-facing offsets.
- **CMake FetchContent for approved dependencies.** CMake can download GLFW,
  GLM, spdlog, and Dear ImGui when they are not available locally. The Vulkan
  SDK remains a required system dependency because the renderer needs the
  loader, headers, `glslc`, and a working driver or portability layer.

## Known Limitations

- **No automated render tests.** Current verification is build/syntax based and
  manual visual inspection. There are no screenshot regression tests or GPU
  correctness tests.
- **One frame in flight.** The renderer uses a single frame fence and one
  acquired image at a time. This is simpler but leaves possible throughput on
  the table.
- **No dedicated transfer staging path.** Some buffers are populated through
  host-visible memory instead of device-local buffers plus staging uploads.
- **Compute and graphics are serialized.** Ocean compute runs in the same
  command buffer before scene rendering. Async compute is not used.
- **No pipeline cache.** Graphics and compute pipelines are recreated from
  scratch on startup and relevant swapchain rebuilds.
- **Fixed render pass model.** The renderer uses a traditional render pass and
  fixed MSAA choice. It does not use dynamic rendering.
- **Limited material model.** Water optics are tuned for plausible real-time
  appearance, not spectral path-traced accuracy.
- **Resolution choices are constrained.** UI exposes power-of-two FFT
  resolutions from 64 to 512 because the compute FFT assumes radix-2 sizing.
- **Vulkan 1.2-capable environment required.** The application requires a
  Vulkan loader, a compatible device or portability layer, compute shaders, and
  `rgba32f` storage-image support.

# Architecture

WaterRenderer is a small real-time Vulkan application organized around one
public application loop and a renderer implementation split by responsibility.
The public renderer API remains `water::VulkanRenderer`; the implementation
details live behind its private `Impl` type.

## Runtime Flow

1. `src/main.cpp` creates `water::Application` and reports fatal exceptions.
2. `src/Application/Application.cpp` owns GLFW initialization, the window,
   frame timing, camera input, settings updates, ocean rebuilds, and per-frame
   renderer calls.
3. `src/Ocean/TessendorfOcean.cpp` owns CPU-side Tessendorf spectrum seed
   generation. Settings that change the spectral basis rebuild this object and
   increment its revision.
4. `src/Renderer/*` owns Vulkan device state, GPU compute, graphics rendering,
   and ImGui controls.

## Renderer Modules

- `src/Renderer/VulkanRenderer.hpp` is the public renderer facade used by the
  application. It exposes UI drawing, frame rendering, idle waits, and mouse
  capture state.
- `src/Renderer/VulkanRenderer.cpp` is intentionally thin. It constructs the
  private implementation and delegates public calls.
- `src/Renderer/VulkanRendererInternal.hpp` is the private shared renderer
  header. It defines Vulkan resource wrappers, shader-facing uniform layouts,
  push constants, shared helpers, and `VulkanRenderer::Impl`.
- `src/Renderer/VulkanContext.cpp` owns Vulkan instance/device setup,
  swapchain creation and recreation, memory/buffer/image helpers, command pool
  setup, synchronization objects, and renderer teardown.
- `src/Renderer/OceanCompute.cpp` owns ocean GPU resources, compute descriptor
  sets, spectrum/FFT/finalize compute pipelines, image barriers, and compute
  dispatch.
- `src/Renderer/SceneRendering.cpp` owns render pass/framebuffer creation,
  graphics descriptors, sky and water pipelines, scene uniform updates,
  command-buffer recording, submission, presentation, and timestamp collection.
- `src/Renderer/RendererUI.cpp` owns Dear ImGui setup/teardown, styling, ocean
  and render settings controls, and performance instrumentation panels.

## GPU Data Flow

The ocean starts with CPU-generated spectral seeds in `TessendorfOcean`. When
the ocean resolution or revision changes, `OceanCompute.cpp` uploads the seed
buffer, allocates ping-pong FFT buffers, creates sampled storage images, and
updates the graphics and compute descriptor sets.

Each frame records one command buffer:

1. Transition displacement and normal images into `VK_IMAGE_LAYOUT_GENERAL`.
2. Run the spectrum compute shader.
3. Run horizontal and vertical radix-2 FFT passes over nine complex fields.
4. Run finalization to write displacement and normal images.
5. Transition the images to shader-read layout.
6. Render the analytic sky, tiled water mesh, and ImGui draw data.
7. Present the swapchain image and collect timestamp queries on the next frame.

## Shader Interface

`SceneUniform` and `OceanComputePush` in
`src/Renderer/VulkanRendererInternal.hpp` mirror shader buffer and push-constant
layouts. Static assertions pin the expected layout offsets so accidental CPU/GPU
ABI drift is caught at compile time.

Shader sources live in `shaders/`. CMake compiles them with the Vulkan SDK
`glslc` into `${build_dir}/shaders/*.spv`, and the renderer loads those SPIR-V
files through the `WATER_SHADER_DIR` compile definition.

# Thunder Vulkan Backend contract

> Top-level boundaries and dependency direction live in [ARCHITECTURE.md](ARCHITECTURE.md);
> this file is the Vulkan backend implementation contract.

The renderer backend is intentionally narrow and strategy-specific.

## Device tier

The first production PC tier targets modern discrete/integrated Vulkan hardware. Device selection
will score dedicated VRAM, queue topology and required feature support rather than simply choosing
the first discrete GPU.

Required baseline before creating the production renderer:

1. Dynamic rendering
2. Synchronization2
3. Timeline semaphores
4. Buffer device address
5. Shader draw parameters / indirect drawing support
6. Timestamp queries

Optional feature tiers can add descriptor indexing improvements, mesh shaders or other techniques,
but gameplay correctness cannot depend on them.

## Frame path

1. Poll SDL events.
2. Query completed GPU timeline value.
3. Acquire a retired `FrameRing` slot; only wait if all slots remain live.
4. Acquire swapchain image.
5. Consume newest simulation snapshot, if any.
6. Build/compile cached RenderGraph shape as required.
7. Record independent graph batches in parallel once the render worker pool lands.
8. Submit via `vkQueueSubmit2` / Synchronization2.
9. Signal next frame timeline value.
10. Present.

No `vkQueueWaitIdle` or `vkDeviceWaitIdle` is permitted in an ordinary frame.

## Swapchain resize

Resize/out-of-date recreation waits only for resources that must be destroyed, not the entire
engine. Camera/render extent updates are decoupled from simulation. Minimized windows may suspend
presentation while simulation continues.

## Uploads

Map chunks and assets use a persistently mapped staging ring. Upload requests are prioritized and
capped by a per-frame byte budget. Camera motion can therefore never trigger an unbounded upload
spike that causes visible hitches.

## Grand-strategy GPU scene

Long-term renderer data is structured around:

- province ID textures / lookup buffers,
- terrain chunk tables,
- instance buffers for settlements, vegetation, farms and industry,
- spline buffers for roads/rail/rivers,
- map-mode parameter buffers,
- indirect draw/dispatch command buffers.

The renderer does not maintain one C++ render object for each visible world object.


## 1.0 RC-GPU live validation draw

When `THUNDER_SHADER_DIR` points to the SPIR-V output produced by `tools/windows/validate_thunder.ps1`, the desktop backend creates graphics pipelines for the streamed world-page mesh, Living Map, map labels and Strategy UI. Terrain/ocean shader contracts remain available for the next renderer stage; they are not claimed as live production passes until their resources and GPU draw paths are bound. Render-finished binary semaphores are owned per swapchain image rather than per CPU frame, so they are not recycled before the presentation engine releases that image.

Strategy UI coordinates are logical window coordinates with a top-left origin
and positive Y downward. A positive Vulkan viewport height uses the same window
orientation; the vertex shader must not apply an additional OpenGL Y flip.
Scissors are converted to swapchain pixels using the current SDL logical/pixel
ratio, which keeps geometry, clipping and input aligned on high-DPI displays.

Shipping text uses `.thunderfont` MSDF metrics and a matching `THUNDERIMG1` atlas.
The world map is separate: Vulkan accepts only `world.thunderworld`, streams the
province/coast + height + lake-mask page family, and never uploads the legacy
full-world map `.thunderimg` assets. Pack opening, page admission and CPU decode
live in `WorldMapPageStreamer`; the Vulkan translation unit only records the
bounded atlas uploads, page-mesh instance buffer and their image barriers.
The fragment shader derives screen-space distance coverage from `fwidth`, so
thin strokes do not disappear when DPI or scripted font size changes. See
`SCRIPT_FIRST_CONTENT.md` for the content-owned font pipeline.

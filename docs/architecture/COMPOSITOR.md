# VantaOS compositor and display presentation

## Current implementation

VantaOS already has a kernel-owned software composition path. Applications submit bounded retained rectangle/text commands through the GUI syscall server; they do not receive a pointer to the scanout framebuffer. The kernel draws the desktop and visible windows into a persistent shadow buffer, tracks damage in 16x16-pixel tiles, then copies only damaged tiles to the boot-provided framebuffer.

The compositor frame contract is:

1. `graphics_begin_frame()` switches drawing to the shadow surface and clears the per-frame damage map.
2. Desktop/window/cursor drawing modifies the shadow surface. The drawing primitives mark affected tiles.
3. `graphics_end_frame()` copies damaged tiles to the scanout surface and restores the public framebuffer pointer.
4. Cursor-only movement and terminal text updates use the same shadow surface; they must not write save-under pixels or window content directly to scanout.

This avoids clearing/repainting the visible framebuffer in multiple drawing steps. If the shadow surface cannot be provisioned, desktop composition now fails closed and retains the last complete frame instead of silently reverting to direct scanout drawing. The damage-rectangle clipping math is shared with a host regression test for negative origins, edge clipping, tile boundaries, empty rectangles, and large coordinates.

It is **not an atomic page flip**: the current Multiboot/UEFI GOP/Bochs framebuffer contract does not expose a validated VBlank wait or display-page-flip operation, so copying damaged tiles can still tear. Do not describe the current path as VSync-complete.

## NVX feasibility

NVX is not currently a usable compositor backend. The current NVIDIA GA10x work has host-tested GSP/RM protocol, GPFIFO, and Ampere copy-engine command-building primitives, but physical GSP-RM startup, complete channel/memory provisioning, and GPU-written completion have not been verified on an RTX 3060.

The copy engine could eventually accelerate a **copy/blit** stage if all of the following are true:

- GSP-RM successfully initializes on real hardware.
- NVX can allocate/map the channel, USERD/GPFIFO, completion semaphore, and source/destination memory with valid GPU virtual addresses.
- The display surface is actually addressable by the GPU under a supported memory model; a CPU framebuffer pointer alone is not enough.
- DMA coherence/cache maintenance and GPU completion are correct, and failures fall back to the software path.
- A physical test confirms the GPU wrote the expected output and that the display consumes the destination surface safely.

A CE copy is not a general compositor: it does not perform alpha blending, window clipping/z-order composition, or display modesetting. A full GPU compositor would additionally need a supported graphics/compute pipeline and verified shader execution. NVX also does not currently provide the display controller's VBlank/page-flip API. Therefore the current implementation intentionally remains software-rendered and does not submit desktop frames to NVX.

## Next display milestones

1. Keep every rendering path on the shadow surface and damage tracker.
2. Damage clipping and tile-boundary geometry now have deterministic host tests; retain QEMU screenshot and boot regressions and add direct old/new cursor-footprint assertions when the damage bitmap is separated into a host-testable unit.
3. Introduce a display-present interface with explicit capabilities: software copy, VBlank wait, and page flip. Report only capabilities implemented by a real display backend.
4. Add a real display driver/presentation mechanism before promising tear-free VSync.
5. Add NVX acceleration only behind a capability-gated backend after real hardware initialization and output verification. Keep the software compositor as the mandatory fallback.

## Limits

- Current app draw lists are retained rectangle/text commands, not shared GPU surfaces.
- Damage tracking reduces copied pixels but does not guarantee that a copy is synchronized with scanout.
- There is no verified hardware cursor plane, display interrupt/VBlank source, GPU page flip, alpha blending, or NVX-accelerated desktop composition yet.

// Direct PS4 VideoOut bring-up. This replaces the crashing Piglet/EGL path on
// firmware 13.02 with the framebuffer API used by OpenOrbis's graphics sample.

#include <orbis/SystemService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

#include <stdint.h>
#include <stddef.h>

uint64_t sceLibcHeapSize = 8 * 1024 * 1024;

enum { WIDTH = 1920, HEIGHT = 1080, BUFFER_COUNT = 2 };

static void park_error(const char *message, int code)
{
    sceKernelDebugOutText(0, "[stremio-ps4] %s: 0x%08x\n", message, (unsigned)code);
    for (;;)
        sceKernelUsleep(100000);
}

int main(void)
{
    const size_t frame_size = (size_t)WIDTH * HEIGHT * 4;
    const size_t alignment = 0x200000;
    const size_t allocation_size =
        ((frame_size * BUFFER_COUNT + alignment - 1) / alignment) * alignment;

    int video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (video < 0)
        park_error("sceVideoOutOpen", video);

    off_t direct_offset = 0;
    int result = sceKernelAllocateDirectMemory(
        0, sceKernelGetDirectMemorySize(), allocation_size, alignment, 3, &direct_offset);
    if (result < 0)
        park_error("sceKernelAllocateDirectMemory", result);

    void *video_memory = 0;
    result = sceKernelMapDirectMemory(
        &video_memory, allocation_size, 0x33, 0, direct_offset, alignment);
    if (result < 0)
        park_error("sceKernelMapDirectMemory", result);

    void *buffers[BUFFER_COUNT];
    buffers[0] = video_memory;
    buffers[1] = (uint8_t *)video_memory + frame_size;

    OrbisVideoOutBufferAttribute attribute;
    sceVideoOutSetBufferAttribute(
        &attribute, 0x80000000, 1, 0, WIDTH, HEIGHT, WIDTH);
    result = sceVideoOutRegisterBuffers(video, 0, buffers, BUFFER_COUNT, &attribute);
    if (result < 0)
        park_error("sceVideoOutRegisterBuffers", result);

    // ARGB purple matching the Stremio palette. Fill both buffers so the
    // compositor can never expose an uninitialized back buffer.
    for (int buffer = 0; buffer < BUFFER_COUNT; ++buffer)
    {
        uint32_t *pixels = (uint32_t *)buffers[buffer];
        for (size_t pixel = 0; pixel < (size_t)WIDTH * HEIGHT; ++pixel)
            pixels[pixel] = 0xff291047u;
    }

    sceVideoOutSetFlipRate(video, 0);
    result = sceVideoOutSubmitFlip(video, 0, ORBIS_VIDEO_OUT_FLIP_VSYNC, 1);
    if (result < 0)
        park_error("sceVideoOutSubmitFlip", result);

    sceSystemServiceHideSplashScreen();
    sceKernelDebugOutText(0, "[stremio-ps4] direct VideoOut frame presented\n");
    for (;;)
        sceKernelUsleep(100000);
}

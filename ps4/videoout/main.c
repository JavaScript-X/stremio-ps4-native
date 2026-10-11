// Direct PS4 VideoOut bring-up. This replaces the crashing Piglet/EGL path on
// firmware 13.02 with the framebuffer API used by OpenOrbis's graphics sample.

#include <orbis/SystemService.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
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

static void fill_buffer(void *buffer, uint32_t color)
{
    uint32_t *pixels = (uint32_t *)buffer;
    for (size_t pixel = 0; pixel < (size_t)WIDTH * HEIGHT; ++pixel)
        pixels[pixel] = color;
}

static int open_pad(void)
{
    int result = scePadInit();
    if (result != 0)
        return result;
    OrbisUserServiceInitializeParams parameters = {0};
    parameters.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    result = sceUserServiceInitialize(&parameters);
    if (result != 0 && result != (int)0x80960003)
        return result;
    int user = -1;
    result = sceUserServiceGetInitialUser(&user);
    if (result != 0)
        return result;
    return scePadOpen(user, 0, 0, 0);
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

    fill_buffer(buffers[0], 0xff291047u);
    fill_buffer(buffers[1], 0xff291047u);

    sceVideoOutSetFlipRate(video, 0);
    result = sceVideoOutSubmitFlip(video, 0, ORBIS_VIDEO_OUT_FLIP_VSYNC, 1);
    if (result < 0)
        park_error("sceVideoOutSubmitFlip", result);

    sceSystemServiceHideSplashScreen();
    sceKernelDebugOutText(0, "[stremio-ps4] direct VideoOut frame presented\n");
    const int pad = open_pad();
    if (pad < 0)
        park_error("scePadOpen", pad);

    int active_buffer = 0;
    uint32_t previous_buttons = 0;
    for (;;)
    {
        OrbisPadData state = {0};
        if (scePadReadState(pad, &state) == 0)
        {
            const uint32_t pressed = state.buttons & ~previous_buttons;
            previous_buttons = state.buttons;
            if (pressed & ORBIS_PAD_BUTTON_CROSS)
            {
                active_buffer ^= 1;
                fill_buffer(buffers[active_buffer], active_buffer ? 0xff6f35ddu : 0xff291047u);
                sceVideoOutSubmitFlip(video, active_buffer, ORBIS_VIDEO_OUT_FLIP_VSYNC,
                                      (int64_t)(active_buffer + 2));
            }
            if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
            {
                scePadClose(pad);
                sceSystemServiceLoadExec("exit", 0);
            }
        }
        sceKernelUsleep(100000);
    }
}

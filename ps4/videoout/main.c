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

static void draw_rect(void *buffer, int x, int y, int width, int height, uint32_t color)
{
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > WIDTH) width = WIDTH - x;
    if (y + height > HEIGHT) height = HEIGHT - y;
    if (width <= 0 || height <= 0) return;
    uint32_t *pixels = (uint32_t *)buffer;
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column)
            pixels[(size_t)row * WIDTH + column] = color;
}

// Compact 5x7 capitals, encoded one column per byte (low seven bits are rows).
static const uint8_t FONT[27][5] = {
    {0,0,0,0,0}, {0x7e,0x09,0x09,0x09,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c}, {0x7f,0x49,0x49,0x49,0x41},
    {0x7f,0x09,0x09,0x09,0x01}, {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01}, {0x7f,0x08,0x14,0x22,0x41},
    {0x7f,0x40,0x40,0x40,0x40}, {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06}, {0x3e,0x41,0x51,0x21,0x5e},
    {0x7f,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f}, {0x3f,0x40,0x38,0x40,0x3f},
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};

static void draw_text(void *buffer, int x, int y, const char *text, int scale, uint32_t color)
{
    for (; *text; ++text, x += 6 * scale)
    {
        int glyph = *text >= 'A' && *text <= 'Z' ? *text - 'A' + 1 : 0;
        for (int column = 0; column < 5; ++column)
            for (int row = 0; row < 7; ++row)
                if (FONT[glyph][column] & (1u << row))
                    draw_rect(buffer, x + column * scale, y + row * scale, scale, scale, color);
    }
}

static void draw_screen(void *buffer, int selected)
{
    static const char *labels[5] = {"BOARD", "DISCOVER", "LIBRARY", "ADDONS", "SETTINGS"};
    fill_buffer(buffer, 0xff10071fu);
    draw_rect(buffer, 0, 0, WIDTH, 126, 0xff1d0d35u);
    draw_text(buffer, 76, 42, "STREMIO PS", 6, 0xfff1ebffu);

    for (int index = 0; index < 5; ++index)
    {
        const int x = 70 + index * 365;
        const uint32_t panel = index == selected ? 0xff6f35ddu : 0xff2a2040u;
        draw_rect(buffer, x, 210, 330, 148, panel);
        if (index == selected)
        {
            draw_rect(buffer, x, 210, 330, 5, 0xffd8c4ffu);
            draw_rect(buffer, x, 353, 330, 5, 0xffd8c4ffu);
        }
        draw_text(buffer, x + 28, 264, labels[index], 4, 0xffffffffu);
    }

    draw_text(buffer, 76, 470, "PS FOUR NATIVE UI", 5, 0xffc8b5e8u);
    draw_rect(buffer, 76, 550, 1768, 310, 0xff1c142cu);
    draw_text(buffer, 126, 610, labels[selected], 7, 0xffffffffu);
    draw_text(buffer, 126, 730, "DPAD TO MOVE", 4, 0xffb7a8cbu);
    draw_text(buffer, 126, 785, "CIRCLE TO EXIT", 4, 0xffb7a8cbu);
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

    draw_screen(buffers[0], 0);
    draw_screen(buffers[1], 0);

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
    int selected = 0;
    int64_t flip_id = 2;
    uint32_t previous_buttons = 0;
    for (;;)
    {
        OrbisPadData state = {0};
        if (scePadReadState(pad, &state) == 0)
        {
            const uint32_t pressed = state.buttons & ~previous_buttons;
            previous_buttons = state.buttons;
            if (pressed & ORBIS_PAD_BUTTON_LEFT)
            {
                selected = (selected + 4) % 5;
                active_buffer ^= 1;
                draw_screen(buffers[active_buffer], selected);
                sceVideoOutSubmitFlip(video, active_buffer, ORBIS_VIDEO_OUT_FLIP_VSYNC,
                                      flip_id++);
            }
            if (pressed & ORBIS_PAD_BUTTON_RIGHT)
            {
                selected = (selected + 1) % 5;
                active_buffer ^= 1;
                draw_screen(buffers[active_buffer], selected);
                sceVideoOutSubmitFlip(video, active_buffer, ORBIS_VIDEO_OUT_FLIP_VSYNC,
                                      flip_id++);
            }
            if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
            {
                scePadClose(pad);
                sceSystemServiceLoadExec("exit", 0);
            }
        }
        sceKernelUsleep(16000);
    }
}

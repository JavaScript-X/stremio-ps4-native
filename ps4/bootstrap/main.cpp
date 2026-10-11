// PS4 bring-up executable for the Stremio native port.
//
// This intentionally exercises only the platform foundation: module loading,
// Piglet/EGL display creation, DualShock 4 input and persistent logging. The
// full Stremio UI replaces this executable once the HUI GLES2 renderer is
// ready. Circle exits so a hardware tester never has to force-close it.

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

// Keep the OpenOrbis include order used by its Piglet sample. Some older SDK
// images rely on sys/types.h defining the BSD aliases consumed by libkernel.
#include <orbis/Pigletv2VSH.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>

namespace
{
constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr const char *kLogDirectory = "/data/stremio-ps4";
constexpr const char *kLogPath = "/data/stremio-ps4/boot.log";

FILE *g_log = nullptr;

void log_line(const char *message)
{
    sceKernelDebugOutText(0, "[stremio-ps4] %s\n", message);
    if (g_log)
    {
        std::fprintf(g_log, "%s\n", message);
        std::fflush(g_log);
    }
}

bool load_piglet_modules(int &piglet, int &shaders)
{
    const char *sandbox = sceKernelGetFsSandboxRandomWord();
    if (!sandbox)
    {
        log_line("cannot resolve the filesystem sandbox");
        return false;
    }
    char piglet_path[512]{};
    char shaders_path[512]{};
    std::snprintf(piglet_path, sizeof(piglet_path), "/%s/common/lib/libScePigletv2VSH.sprx", sandbox);
    std::snprintf(shaders_path, sizeof(shaders_path), "/%s/common/lib/libScePrecompiledShaders.sprx", sandbox);
    int module_result = 0;
    piglet = static_cast<int>(sceKernelLoadStartModule(
        piglet_path, 0, nullptr, 0, nullptr, &module_result));
    if (piglet < 0)
    {
        log_line("cannot load libScePigletv2VSH");
        return false;
    }
    shaders = static_cast<int>(sceKernelLoadStartModule(
        shaders_path, 0, nullptr, 0, nullptr, &module_result));
    if (shaders < 0)
    {
        log_line("cannot load libScePrecompiledShaders");
        return false;
    }
    return true;
}

bool create_display(EGLDisplay &display, EGLSurface &surface, EGLContext &context)
{
    OrbisPglConfig pgl{};
    pgl.size = sizeof(pgl);
    pgl.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT | ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60;
    pgl.processOrder = 1;
    pgl.systemSharedMemorySize = 128 * 1024 * 1024;
    pgl.videoSharedMemorySize = 256 * 1024 * 1024;
    pgl.maxMappedFlexibleMemory = 96 * 1024 * 1024;
    pgl.drawCommandBufferSize = 1 * 1024 * 1024;
    pgl.lcueResourceBufferSize = 1 * 1024 * 1024;
    pgl.dbgPosCmd_0x40 = kWidth;
    pgl.dbgPosCmd_0x44 = kHeight;
    pgl.unk_0x5C = 2;
    if (!scePigletSetConfigurationVSH(&pgl))
        return false;

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0, count = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_ES_API))
        return false;

    const EGLint config_attributes[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
    EGLConfig config{};
    if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || count < 1)
        return false;

    OrbisPglWindow window{0, kWidth, kHeight};
    const EGLint window_attributes[] = {EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE};
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    surface = eglCreateWindowSurface(display, config, &window, window_attributes);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context))
        return false;
    eglSwapInterval(display, 1);
    return true;
}

int open_pad()
{
    if (scePadInit() != 0)
        return -1;
    OrbisUserServiceInitializeParams parameters{};
    parameters.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&parameters);
    int user = -1;
    if (sceUserServiceGetInitialUser(&user) != 0)
        return -1;
    return scePadOpen(user, 0, 0, nullptr);
}
} // namespace

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    mkdir(kLogDirectory, 0777);
    g_log = std::fopen(kLogPath, "a");
    log_line("P1 bootstrap starting");

    int piglet = -1, shaders = -1;
    if (!load_piglet_modules(piglet, shaders))
        return 10;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    if (!create_display(display, surface, context))
    {
        log_line("Piglet/EGL display creation failed");
        return 11;
    }

    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    sceSystemServiceHideSplashScreen();
    const int pad = open_pad();
    if (pad < 0)
    {
        log_line("DualShock 4 open failed");
        return 12;
    }

    log_line("display and controller ready; Circle exits");
    bool running = true;
    while (running)
    {
        OrbisPadData state{};
        if (scePadReadState(pad, &state) == 0 && (state.buttons & ORBIS_PAD_BUTTON_CIRCLE))
            running = false;

        glViewport(0, 0, kWidth, kHeight);
        glClearColor(0.16f, 0.06f, 0.28f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!eglSwapBuffers(display, surface))
        {
            log_line("eglSwapBuffers failed");
            break;
        }
        sceKernelUsleep(1000);
    }

    log_line("clean shutdown");
    scePadClose(pad);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    if (g_log)
        std::fclose(g_log);
    return 0;
}

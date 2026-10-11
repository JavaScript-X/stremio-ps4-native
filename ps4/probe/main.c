// Minimal PS4 loader probe. This deliberately avoids Piglet, EGL, controller,
// filesystem and C++ code so a hardware test can separate package/loader
// compatibility from graphics initialization failures.

#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

#include <stdint.h>

uint64_t sceLibcHeapSize = 4 * 1024 * 1024;

int main(void)
{
    sceKernelDebugOutText(0, "[stremio-ps4] loader probe entered main\n");
    sceSystemServiceHideSplashScreen();

    // A successful probe intentionally remains on a black screen. Returning
    // from main would itself invoke the crash reporter on some homebrew setups.
    for (;;)
        sceKernelUsleep(100000);
}

# PS4 native port

This directory is the PS4 platform layer for the application. The port starts
from the GPL-3.0 PS5 application and shares its Stremio API, account, catalogue,
library, addon, torrent, subtitle and UI model code. It does **not** use code
from `JavaScript-X/stremio-ps4` or any other JavaScript-X repository.

## Supported target

- Jailbroken retail PS4 and PS4 Pro
- OpenOrbis PS4 Toolchain
- Fake PKG installation
- 1920x1080 UI (PS4 Pro 4K output is a later, separately tested target)

## Build the P1 bootstrap

Install the public OpenOrbis PS4 Toolchain, then from WSL/Linux:

```sh
export OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4Toolchain
cd ps4
make
```

For a minimal loader-isolation package with no graphics, controller,
filesystem or C++ dependencies:

```sh
make BUILD_MODE=probe
```

The loader probe is version 0.05 and intentionally remains on a black screen.
If it stays open, the package and executable loader are compatible and the
fault is inside the Piglet path. If it still reports `CE-34878-0`, the issue is
below the application code (firmware/homebrew loader, FSELF or package
compatibility).

Firmware 13.02 with GoldHEN 2.4b18.12 passes that loader probe. Piglet/EGL is
therefore excluded from the PS4 port. Build the direct framebuffer probe with:

```sh
make BUILD_MODE=videoout
```

Version 0.06 displayed a stable Stremio-purple 1080p frame on firmware 13.02
with GoldHEN 2.4b18.12, validating direct VideoOut and confirming Piglet as the
old crash boundary. Version 0.07 validated DualShock 4 input and clean shutdown.
Version 0.08 adds the first native framebuffer UI shell: five labeled Stremio
sections, visible focus, D-pad navigation and Circle exit.

The output is `dist-ps4/IV0000-BREW00991_00-STREMIOPS4000000.pkg`.
It is a bring-up build, not the Stremio application: on compatible jailbroken
hardware it should open a purple 1080p Piglet surface, initialize a DualShock 4,
append diagnostics to `/data/stremio-ps4/boot.log`, and exit with Circle.

Version 0.03 parks on startup failure instead of returning into the PS4 crash
reporter, logs the failing stage and error code, tolerates the optional shader
module during this clear-screen probe, and closes through System Service rather
than returning from `main()`. It also removes unused `libSceShellCoreUtil` and
`libScePrecompiledShaders` loader imports so firmware without those exports can
still enter `main()`. If it does not reach the purple screen, retrieve
`/data/stremio-ps4/boot.log` over FTP before closing the title.

## Port status

The common application code now has an explicit PS4 identity and is protected
by an automated platform audit. The console backends below are still required
before the app can be called installable or feature complete:

| Area | PS5 implementation | PS4 replacement | Status |
| --- | --- | --- | --- |
| Package | `.ffpfsc`, PS5 param JSON | OpenOrbis fake PKG/SFO | bring-up implementation |
| GPU/UI | PS5 OpenGL over AGC | Piglet/OpenGL ES or OpenGNM | EGL bring-up implementation |
| Input | DualSense `libScePad` | DualShock 4 `libScePad` | bring-up implementation |
| Keyboard | PS5 IME dialog | PS4 IME dialog | pending |
| Audio | PS5 `libSceAudioOut` | PS4 `libSceAudioOut` | pending |
| Video | `libSceVideodec2` | `libSceAvPlayer`/PS4 decoder | pending |
| Network | PS5 resolver/curl shims | PS4 Net/HTTP/curl port | pending |
| Storage | `/download0/stremio` | `/data/...` or save-data mount | pending |
| QA | PS5 hardware | PS4 + PS4 Pro hardware | pending |

See [`PORTING.md`](../PORTING.md) for the implementation gates and acceptance
criteria. A green source audit is not a working PKG; hardware validation is a
required release gate.

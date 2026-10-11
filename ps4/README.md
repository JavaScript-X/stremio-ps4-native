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

## Port status

The common application code now has an explicit PS4 identity and is protected
by an automated platform audit. The console backends below are still required
before the app can be called installable or feature complete:

| Area | PS5 implementation | PS4 replacement | Status |
| --- | --- | --- | --- |
| Package | `.ffpfsc`, PS5 param JSON | OpenOrbis fake PKG/SFO | pending |
| GPU/UI | PS5 OpenGL over AGC | Piglet/OpenGL ES or OpenGNM | pending |
| Input | DualSense `libScePad` | DualShock 4 `libScePad` | pending |
| Keyboard | PS5 IME dialog | PS4 IME dialog | pending |
| Audio | PS5 `libSceAudioOut` | PS4 `libSceAudioOut` | pending |
| Video | `libSceVideodec2` | `libSceAvPlayer`/PS4 decoder | pending |
| Network | PS5 resolver/curl shims | PS4 Net/HTTP/curl port | pending |
| Storage | `/download0/stremio` | `/data/...` or save-data mount | pending |
| QA | PS5 hardware | PS4 + PS4 Pro hardware | pending |

See [`PORTING.md`](../PORTING.md) for the implementation gates and acceptance
criteria. A green source audit is not a working PKG; hardware validation is a
required release gate.

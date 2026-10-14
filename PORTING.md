# PS4 port plan and acceptance gates

The goal is feature parity with the upstream PS5 client while keeping the new
PS4 project independent from every pre-existing JavaScript-X project. The only
application source base is the upstream GPL-3.0 repository recorded in Git
history.

## Rules

1. Do not copy code, commits, binaries or assets from
   `JavaScript-X/stremio-ps4` or another JavaScript-X repository.
2. Use public, redistributable OpenOrbis and open-source dependencies only. Do
   not commit Sony SDK files, keys, firmware files or proprietary modules.
3. Keep common Stremio behavior in `src/`, `gl/` and `ui/`; isolate console APIs
   under `ps4/` and `third_party/hui/src/platform/ps4/`.
4. Never advertise a feature as working until it has been exercised on a real
   PS4. Emulator checks are useful but are not a substitute for hardware.

## Delivery stages

- **P0 — source separation:** platform identity, audit, repository and branch.
- **P1 — boots:** OpenOrbis build emits a PKG, launches, draws the boot screen,
  reads DualShock 4 input and writes persistent logs.
- **P2 — Stremio UI:** Board, Discover, Library, Addons, Settings, search and
  account code work with the PS4 IME and network stack.
- **P3 — direct playback:** HTTP(S), HLS/MP4/MKV demux, hardware H.264/HEVC
  where supported, software fallback, audio, subtitles, seeking and resume.
- **P4 — torrents:** trackers, DHT, PEX, rolling cache, seeking, peer validation
  and UPnP behavior are validated on PS4.
- **P5 — parity/release:** every row in the matrix below passes on base PS4 and
  PS4 Pro; licenses and reproducible build instructions ship in the PKG.

## Feature-parity matrix

| Feature | Base PS4 | PS4 Pro | Required evidence |
| --- | --- | --- | --- |
| QR/link-code login and account sync | ⬜ | ⬜ | login, restart, token refresh |
| Board/Discover/Library/Addons/Settings | ⬜ | ⬜ | navigation capture + log |
| Search with system keyboard | ⬜ | ⬜ | Unicode input cases |
| Movie/series/season/episode browsing | ⬜ | ⬜ | metadata and watched state |
| Direct HTTP/HLS playback | ⬜ | ⬜ | 30-minute soak per codec |
| Built-in torrent streaming | ⬜ | ⬜ | cold start, seek, cache recovery |
| H.264/HEVC hardware decode | ⬜ | ⬜ | decoder mode and dropped frames |
| Software codec fallback | ⬜ | ⬜ | VP9/unsupported sample |
| Subtitles including Arabic/Hebrew | ⬜ | ⬜ | shaping, RTL and delay |
| Audio-track selection | ⬜ | ⬜ | AAC/AC3/multiple tracks |
| Resume/Continue Watching/next episode | ⬜ | ⬜ | restart and completion cases |
| Pause on PS button/system suspend | ⬜ | ⬜ | suspend/resume cycle |

## Immediate technical risks

- The PS5 renderer uses OpenGL 4.6 features supplied by a PS5 AGC translation
  layer. OpenOrbis Piglet is OpenGL ES-oriented, so shaders, texture formats and
  batching need a deliberate compatibility pass.
- `libSceVideodec2` is PS5-only. PS4 playback must use a legal OpenOrbis-facing
  backend such as `libSceAvPlayer` or a documented decoder path, with FFmpeg as
  a performance-limited fallback.
- A base PS4 has much less CPU headroom than PS5. Torrent hashing, software
  decoding, image decode and UI uploads need profiling together, not in
  isolation.

## P1 bring-up implementation

The OpenOrbis fake-PKG and generic application FSELF are hardware validated on
firmware 13.02 with GoldHEN 2.4b18.12. The loader probe and direct 1080p
`libSceVideoOut` framebuffer remain stable. Piglet/EGL crashes before presenting
a frame in that environment, so the PS4 renderer will use VideoOut (or an
OpenGNM backend built on it), not Piglet. The existing PS5 HUI renderer is not
connected to this backend yet.

## Current integration checkpoint (0.36)

The PS4 build now compiles the upstream `src/stremio.cpp` addon and stream
model behind a small C boundary. Direct links enter an on-demand AVPlayer
backend; allocation, audio, lifecycle, seek and frame-conversion paths have
host-side sanitizer tests. Network operations run outside the render loop and
Library records persist independently of the current catalog.

No unchecked matrix row is considered passed by compilation alone. Direct
media needs codec/container testing on both PS4 models. FFmpeg fallback, IME,
sound and suspend integrations still need PS4 platform implementations.

The 0.32 follow-up connects external addon subtitles, the shared PS5 subtitle
parsers, AVPlayer audio-stream selection and next-episode autoplay. Subtitle
format parsing is sanitizer-tested; full Unicode shaping/font coverage and the
new controls still require hardware acceptance.

The 0.33 follow-up compiles the upstream torrent peer engine for PS4 and joins
it to AVPlayer's seekable replacement-file callbacks. UDP trackers, BEP 9
metadata, PEX, rolling cache and SHA-1 piece verification are implemented and
host/build tested. The matrix remains unchecked until cold start, playback,
seek, cancellation and cache recovery pass on real PS4 hardware. DHT, HTTP
tracker announces and UPnP are not enabled in the PS4 build yet.

The 0.35 follow-up replaces torrent-engine worker creation with explicit
pthread-backed workers, removes thread-local RNG state, hardens file callback
bounds/cancellation, and adds startup stage logging. The PS4-profile engine
passes host sanitizer tests for startup, cached metadata, cancellation and
restarts. Cold-start torrent playback remains unverified on physical hardware.
The 0.36 follow-up uses bounded polling instead of the PS4 timed condition-
variable wait while awaiting metadata and adds precise UDP tracker failure
logging; console acceptance remains outstanding.

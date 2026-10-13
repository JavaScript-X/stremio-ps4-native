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
Version 0.09 makes a larger step toward the PS5 frontend: it packages and
decodes the PS5 port's original navigation artwork, adopts its left rail and
theme, implements five distinct screens, search overlay, content-grid
navigation, settings toggles, L1/R1 switching, back routing and clean exit.
Version 0.10 adds the PS4 network/SSL/HTTP stack, loads the live Cinemeta movie
catalog used by the PS5 port, parses its titles and poster URLs, downloads and
decodes JPEG posters, and keeps the shell usable with offline fallback content.
Version 0.11 follows Cinemeta's current canonical catalog host instead of
rejecting its HTTP 307 redirect and exposes precise network/request/parse state
in the Board status line for hardware diagnosis.
Version 0.12 requests Metahub's direct JPEG representation for every poster,
avoiding both redirects and unsupported WebP responses. It also clips long
card labels and shows live catalog titles consistently in Discover.
Version 0.13 anchors each parsed card to its Cinemeta metadata object so nested
rating, genre and link names cannot replace movie titles. Long titles now use
a smaller card-label scale rather than overflowing or disappearing.
Version 0.14 loads separate live movie and series catalogs with ten poster
cards, adds vertical row navigation, parses IMDb id/type/year/description, and
opens a native title-details screen with Cross and returns with Circle.
Version 0.15 imports the PS5 port's original wordmark and DualShock prompt
artwork. Search, global navigation, page switching, selection, back, and the
details action now use packaged Triangle/D-pad/L1/R1/Cross/Circle/Play assets.
Version 0.16 replaces the search placeholder with a controller-driven keyboard,
editable queries, delete/space/go actions, live Cinemeta movie search, five
poster results, result navigation, and result-to-details routing.
Version 0.17 ports the PS5 metadata-on-open flow for full descriptions, runtime,
rating and genres. Square now saves/removes titles in a working local-session
Library, and Find Streams opens a routed source screen that links to Addons when
no stream provider has been configured.
Version 0.18 persists settings and saved IMDb ids in PS4 app storage, restores
matching Library entries on launch, makes Discover switch live Movies/Series
with Square, and adds Options-driven catalog refresh with the PS5 Options asset.
Version 0.19 adds Stremio device linking under Settings. It creates a short
code, checks approval, stores linked state separately from normal preferences,
restores it after relaunch, and supports unlinking. Public Domain on the Addons
page can now be enabled, disabled, and validated against its live manifest.
Authenticated library/addon sync is prepared but not yet claimed complete.
Version 0.20 ports the PS5 interface styling into the direct VideoOut renderer:
rounded panels and pills, violet layered backgrounds, softer navigation states,
PS5-style poster cards and focus rings, a contextual two-column Settings page,
and redesigned account, addon, library, search, detail, and stream layouts.
Version 0.21 replaces the bring-up bitmap alphabet with a cached native atlas
generated from the same bundled Inter typeface used by the PS5 UI. It adds real
glyph metrics and punctuation, mixed-case presentation, and properly clipped
rounded poster artwork without changing the stable VideoOut backend.
Version 0.22 rebuilds Addons as a PS5-style two-column tile grid with matching
controller navigation, enriches Library with filters and counts, fills the title
page with an About panel and metadata badges, and makes stream empty states
reflect the source that was actually checked.
Version 0.23 connects Find Streams to the live Public Domain stream endpoint,
parses real source names, sizes and info hashes, displays navigable source rows,
and distinguishes no-match, disabled-addon, and request-failure states. Search
now combines movie and series results in one screen.
Version 0.24 fixes the decoded-image red/blue channel order in both standard and
rounded blitters. Posters and packaged artwork now retain their source colours.
It also imports the PS5 small play/check, pause, filter-chevron and large-logo
assets for richer saved, account, Discover and stream states.
Version 0.25 adds a native series episode browser backed by live Cinemeta video
metadata. Series now open season and episode selection before streams, support
L1/R1 season switching and scrolling D-pad navigation, and resolve addons with
the selected episode's full Stremio video id.
Version 0.26 ports more of the PS5 presentation layer: live background and logo
artwork on title flows, animated focus-card lift and colour pulses, ambient
backdrop motion, multi-frame rendering after interaction, and toast feedback for
saved titles, settings, addons, account actions, and catalog refreshes.
Version 0.27 optimizes animation scheduling: seven-frame transitions replace the
long redraw window, toasts no longer redraw the full 1080p surface while they
remain visible, and live backdrop blending is deferred to the settled frame.
Settings now includes a native measured FPS/frame-time overlay. GoldHEN's Game
Overlay remains separately compatible through the standard VideoOut flip path;
the app does not install a competing SubmitFlip hook.

Version 0.28 moves software composition into cacheable CPU memory and copies only
completed frames into VideoOut scanout memory. Opaque artwork bypasses alpha
blending. This removes the direct-video-memory read/modify/write bottleneck that
the 0.27 performance counter exposed on real PS4 hardware.

Version 0.29 begins the full account-data integration used by the PS5 app. A
linked account now imports its Stremio library through `datastoreGet`, reads the
account addon collection, merges matching saved titles into the native Library,
shows sync totals, refreshes automatically at startup, and supports an explicit
Options-button resync from the account screen.

Version 0.30 expands that integration from matching catalog entries to native
cards for the linked account's library. It imports title identity, type, name
and poster artwork, merges duplicates with live Cinemeta results, supports up to
40 in-memory titles, and adds multi-page Library navigation. The heap budget is
raised for the larger decoded-artwork set while retaining the fast 0.28 canvas.

The output is `dist-ps4/IV0000-BREW00991_00-STREMIOPS4000000.pkg`.
The current package is a native 1080p VideoOut application using DualShock 4
navigation and PS4 networking. Firmware 13.02 with GoldHEN 2.4b18.12 is the
currently confirmed hardware configuration.

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

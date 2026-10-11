#pragma once

// Platform identity shared by the HTTP, torrent and diagnostic layers. Keeping
// these values here prevents PS5 branding and assumptions leaking into the PS4
// build while the feature code remains common to both consoles.
namespace sx::platform
{
#if defined(PLATFORM_PS4)
inline constexpr const char *name = "PS4";
inline constexpr const char *product = "Stremio-PS4";
inline constexpr const char *user_agent =
    "Mozilla/5.0 (PlayStation; PlayStation 4/1.00) Stremio-PS4/0.1-dev";
inline constexpr int decoder_threads = 6;
#elif defined(PLATFORM_PS5)
inline constexpr const char *name = "PS5";
inline constexpr const char *product = "Stremio-PS5";
inline constexpr const char *user_agent =
    "Mozilla/5.0 (PlayStation; PlayStation 5/1.00) Stremio-PS5/1.0";
inline constexpr int decoder_threads = 8;
#else
inline constexpr const char *name = "desktop";
inline constexpr const char *product = "Stremio-preview";
inline constexpr const char *user_agent = "Stremio-preview/1.0";
inline constexpr int decoder_threads = 4;
#endif
} // namespace sx::platform

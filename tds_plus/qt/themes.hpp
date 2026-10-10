// language: C++17, file: themes.hpp, runtime: any (no Qt, no platform headers), target: tds+ UI themes
// Theme and accent tables of the Qt UI. A theme is a handful of base colors; every other surface,
// border and control color is a mix of the window color toward the theme's "tone" by a fixed amount.
// The amounts were fitted to the original hand-picked dark palette, so the default theme (graphite)
// reproduces it within 2/255 per channel. tests/themes_test.cpp checks text contrast for every theme
// and accent.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tds_theme {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

constexpr Rgba rgb(uint32_t hex) {
    return Rgba{static_cast<uint8_t>(hex >> 16), static_cast<uint8_t>(hex >> 8), static_cast<uint8_t>(hex), 255};
}

constexpr bool operator==(Rgba x, Rgba y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; }

inline uint8_t clamp_channel(double v) {
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : std::lround(v));
}

// `from` moved toward `to` by t (0 = from, 1 = to); keeps the alpha of `from`
inline Rgba mix(Rgba from, Rgba to, double t) {
    return Rgba{clamp_channel(from.r + (to.r - from.r) * t), clamp_channel(from.g + (to.g - from.g) * t),
                clamp_channel(from.b + (to.b - from.b) * t), from.a};
}

inline Rgba with_alpha(Rgba c, double alpha) {
    c.a = clamp_channel(alpha * 255);
    return c;
}

// `top` drawn over an opaque `bottom`
inline Rgba over(Rgba top, Rgba bottom) {
    Rgba out = mix(bottom, top, top.a / 255.0);
    out.a = 255;
    return out;
}

inline double channel_luminance(uint8_t v) {
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// WCAG relative luminance and contrast ratio (1..21)
inline double luminance(Rgba c) {
    return 0.2126 * channel_luminance(c.r) + 0.7152 * channel_luminance(c.g) + 0.0722 * channel_luminance(c.b);
}

inline double contrast(Rgba x, Rgba y) {
    const double lx = luminance(x), ly = luminance(y);
    return ((lx > ly ? lx : ly) + 0.05) / ((lx > ly ? ly : lx) + 0.05);
}

struct Palette {
    Rgba window;         // window background, also the "darken" overlay of a picture background
    Rgba surface;        // translucent list panel
    Rgba field;          // translucent search field
    Rgba row_hover;      // translucent hovered list row
    Rgba popup, popup_border;
    Rgba raised;         // tooltip
    Rgba border_strong;  // tooltip / spin box / segment thumb border
    Rgba control;        // selected segment
    Rgba thumb;          // sliding segment indicator
    Rgba control_hover, control_down;
    Rgba input;          // spin box, Controls base color
    Rgba focus;          // keyboard focus ring
    Rgba border, divider, separator;
    Rgba ink, muted, dim;  // text: primary, secondary, tertiary
    Rgba accent, accent_hover, accent_down, on_accent, on_accent_dim;
    Rgba danger;
    Rgba scroll, scroll_active;
    Rgba selection;
    float wave_base[3];  // animated background: color = base + tint * light (tint may be negative)
    float wave_tint[3];
    bool dark;
};

struct ThemeDef {
    const char* id;
    const char* name;
    bool dark;
    Rgba window, tone, ink, muted, dim;
    Rgba accent, accent_hover, accent_down, on_accent, on_accent_dim;
    Rgba danger;
    float wave_base[3];
    float wave_tint[3];
    bool light_surfaces;  // white panels on a light window instead of a mix toward the tone
};

struct AccentDef {
    const char* id;
    const char* name;
    Rgba color;  // ignored for "theme"
};

inline constexpr const char* kDefaultTheme = "graphite";
inline constexpr const char* kThemeAccent = "theme";

inline constexpr ThemeDef kThemes[] = {
    {"graphite", "Graphite", true, rgb(0x09090b), rgb(0xc4c4d6), rgb(0xeaeaec), rgb(0xa0a0a9), rgb(0x7d7d87),
     rgb(0xe6e6e9), rgb(0xfafafa), rgb(0xbdbdc6), rgb(0x151518), rgb(0x64646e), rgb(0xd9a3ac),
     {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.035f}, false},
    {"midnight", "Midnight", true, rgb(0x060a14), rgb(0xa9bde6), rgb(0xe3eaf7), rgb(0xa2afc8), rgb(0x8291ae),
     rgb(0xdce6fa), rgb(0xf2f6ff), rgb(0xaebcd9), rgb(0x0b1020), rgb(0x56647f), rgb(0xe8a8b4),
     {0.004f, 0.008f, 0.022f}, {0.55f, 0.75f, 1.35f}, false},
    {"moss", "Moss", true, rgb(0x070b09), rgb(0xb4c8ba), rgb(0xe4ece6), rgb(0xa2b1a7), rgb(0x829388),
     rgb(0xdcebe0), rgb(0xf1f8f3), rgb(0xafc4b5), rgb(0x0c120e), rgb(0x5a6b60), rgb(0xe3aaaa),
     {0.004f, 0.010f, 0.006f}, {0.70f, 1.0f, 0.80f}, false},
    {"paper", "Paper", false, rgb(0xf3f3f1), rgb(0x26262c), rgb(0x18181b), rgb(0x4b4b53), rgb(0x5f5f68),
     rgb(0x1d1d22), rgb(0x3a3a42), rgb(0x000000), rgb(0xf7f7f5), rgb(0xa1a1aa), rgb(0xb0213a),
     {0.953f, 0.953f, 0.945f}, {-0.55f, -0.55f, -0.50f}, true},
};

inline constexpr AccentDef kAccents[] = {
    {kThemeAccent, "Theme", rgb(0)},
    {"blue", "Blue", rgb(0x5b8def)},
    {"violet", "Violet", rgb(0x9b7cf2)},
    {"green", "Green", rgb(0x3fb27a)},
    {"amber", "Amber", rgb(0xe2a53b)},
    {"rose", "Rose", rgb(0xec6f8e)},
};

inline constexpr size_t kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
inline constexpr size_t kAccentCount = sizeof(kAccents) / sizeof(kAccents[0]);

inline const ThemeDef* find_theme(const char* id) {
    for (const ThemeDef& t : kThemes)
        if (id && std::strcmp(t.id, id) == 0) return &t;
    return nullptr;
}

inline const AccentDef* find_accent(const char* id) {
    for (const AccentDef& a : kAccents)
        if (id && std::strcmp(a.id, id) == 0) return &a;
    return nullptr;
}

// Builds every color of the UI. `accent` may be null or the "theme" accent for the theme's own.
inline Palette make_palette(const ThemeDef& t, const AccentDef* accent) {
    const auto toward = [&t](double amount) { return mix(t.window, t.tone, amount); };
    Palette p{};
    p.window = t.window;
    p.surface = with_alpha(toward(0.045), 0.651);
    p.field = with_alpha(toward(0.066), 0.459);
    p.row_hover = with_alpha(toward(0.153), 0.451);
    p.popup = toward(0.089);
    p.popup_border = toward(0.278);
    p.raised = toward(0.148);
    p.border_strong = toward(0.267);
    p.control = toward(0.186);
    p.thumb = toward(0.179);
    p.control_hover = toward(0.142);
    p.control_down = toward(0.210);
    p.input = toward(0.106);
    p.focus = toward(0.529);
    p.border = toward(0.173);
    p.divider = toward(0.146);
    p.separator = toward(0.248);
    p.scroll = toward(0.352);
    p.scroll_active = toward(0.733);
    p.selection = toward(0.357);
    if (t.light_surfaces) {
        const Rgba white = rgb(0xffffff);
        p.surface = with_alpha(white, 0.72);
        p.field = with_alpha(white, 0.70);
        p.row_hover = with_alpha(toward(0.153), 0.55);
        p.popup = white;
        p.input = white;
        p.raised = white;
    }
    p.ink = t.ink;
    p.muted = t.muted;
    p.dim = t.dim;
    p.danger = t.danger;
    p.dark = t.dark;
    for (int i = 0; i < 3; ++i) {
        p.wave_base[i] = t.wave_base[i];
        p.wave_tint[i] = t.wave_tint[i];
    }
    if (!accent || std::strcmp(accent->id, kThemeAccent) == 0) {
        p.accent = t.accent;
        p.accent_hover = t.accent_hover;
        p.accent_down = t.accent_down;
        p.on_accent = t.on_accent;
        p.on_accent_dim = t.on_accent_dim;
    } else {
        const Rgba c = accent->color, white = rgb(0xffffff), black = rgb(0x0b0b0d);
        p.accent = c;
        p.accent_hover = mix(c, white, 0.14);
        p.accent_down = mix(c, rgb(0x000000), 0.16);
        p.on_accent = contrast(white, c) >= contrast(black, c) ? white : black;
        p.on_accent_dim = mix(p.on_accent, c, 0.45);
    }
    return p;
}

}  // namespace tds_theme

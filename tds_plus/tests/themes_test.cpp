// language: C++17, file: themes_test.cpp, runtime: any, target: tds+ UI themes
#include <cstdio>
#include <cstring>
#include <string>

#include "../qt/themes.hpp"

using namespace tds_theme;

static int failures = 0;
static void check(bool ok, const std::string& name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
    failures += !ok;
}

static bool near(Rgba x, Rgba y, int tolerance) {
    const auto d = [](uint8_t p, uint8_t q) { return p > q ? p - q : q - p; };
    return d(x.r, y.r) <= tolerance && d(x.g, y.g) <= tolerance && d(x.b, y.b) <= tolerance && x.a == y.a;
}

static Rgba argb(uint32_t hex) {
    Rgba c = rgb(hex & 0xffffff);
    c.a = static_cast<uint8_t>(hex >> 24);
    return c;
}

int main() {
    // the math itself
    check(std::abs(contrast(rgb(0x000000), rgb(0xffffff)) - 21.0) < 1e-9, "black on white is 21:1");
    check(std::abs(contrast(rgb(0x777777), rgb(0x777777)) - 1.0) < 1e-9, "a color on itself is 1:1");
    check(mix(rgb(0x000000), rgb(0xffffff), 0.5) == rgb(0x808080), "mix halfway");
    check(over(with_alpha(rgb(0xffffff), 0.5), rgb(0x000000)) == rgb(0x808080), "50% white over black");

    // tables
    check(find_theme(kDefaultTheme) == &kThemes[0], "the default theme exists and is listed first");
    check(find_theme("nope") == nullptr && find_theme(nullptr) == nullptr, "unknown theme ids are not found");
    check(find_accent(kThemeAccent) == &kAccents[0] && find_accent("nope") == nullptr, "accent lookup");
    bool unique = true;
    for (size_t i = 0; i < kThemeCount; ++i)
        for (size_t j = i + 1; j < kThemeCount; ++j) unique = unique && std::strcmp(kThemes[i].id, kThemes[j].id) != 0;
    for (size_t i = 0; i < kAccentCount; ++i)
        for (size_t j = i + 1; j < kAccentCount; ++j) unique = unique && std::strcmp(kAccents[i].id, kAccents[j].id) != 0;
    check(unique, "theme and accent ids are unique");
    bool has_light = false, has_dark = false;
    for (const ThemeDef& t : kThemes) (t.dark ? has_dark : has_light) = true;
    check(has_light && has_dark, "there is at least one light and one dark theme");

    // the default theme keeps the original hand-picked colors (Main.qml before themes)
    const Palette g = make_palette(kThemes[0], nullptr);
    const struct { const char* name; Rgba got; uint32_t original; } originals[] = {
        {"window", g.window, 0xff09090b},       {"ink", g.ink, 0xffeaeaec},
        {"muted", g.muted, 0xffa0a0a9},         {"dim", g.dim, 0xff7d7d87},
        {"accent", g.accent, 0xffe6e6e9},       {"on accent", g.on_accent, 0xff151518},
        {"surface", g.surface, 0xa6111115},     {"field", g.field, 0x75151519},
        {"row hover", g.row_hover, 0x7325252b}, {"popup", g.popup, 0xff19191e},
        {"popup border", g.popup_border, 0xff3c3c45}, {"tooltip", g.raised, 0xff24242a},
        {"strong border", g.border_strong, 0xff3a3a43}, {"selected", g.control, 0xff2b2b32},
        {"thumb", g.thumb, 0xff2a2a30},         {"hover", g.control_hover, 0xff232329},
        {"pressed", g.control_down, 0xff303036}, {"input", g.input, 0xff1c1c22},
        {"focus", g.focus, 0xff6c6c76},         {"border", g.border, 0xff29292f},
        {"divider", g.divider, 0xff242429},     {"separator", g.separator, 0xff37373e},
        {"scroll", g.scroll, 0xff4a4a54},       {"scroll pressed", g.scroll_active, 0xff93939e},
        {"selection", g.selection, 0xff4b4b55},
    };
    for (const auto& o : originals)
        check(near(o.got, argb(o.original), 2), std::string("graphite reproduces the original ") + o.name + " within 2/255");

    // contrast of every theme with every accent
    for (const ThemeDef& t : kThemes) {
        for (const AccentDef& a : kAccents) {
            const Palette p = make_palette(t, &a);
            const std::string tag = std::string(t.id) + "/" + a.id + ": ";
            const Rgba panel = over(p.surface, p.window);
            if (std::strcmp(a.id, kThemeAccent) == 0) {
                check(contrast(p.ink, p.window) >= 7.0, tag + "primary text on the window >= 7:1");
                check(contrast(p.muted, p.window) >= 4.5, tag + "secondary text on the window >= 4.5:1");
                check(contrast(p.dim, p.window) >= 4.5, tag + "tertiary text on the window >= 4.5:1");
                check(contrast(p.ink, panel) >= 7.0, tag + "primary text on the list panel >= 7:1");
                check(contrast(p.dim, panel) >= 4.5, tag + "tertiary text on the list panel >= 4.5:1");
                check(contrast(p.muted, p.popup) >= 4.5, tag + "secondary text in popups >= 4.5:1");
                check(contrast(p.ink, p.control) >= 4.5, tag + "label of a selected segment >= 4.5:1");
                check(contrast(p.muted, p.input) >= 4.5, tag + "secondary text inside inputs >= 4.5:1");
                check(contrast(p.danger, p.window) >= 4.5, tag + "error text >= 4.5:1");
                check(contrast(p.ink, over(p.row_hover, panel)) >= 7.0, tag + "primary text on a hovered row >= 7:1");
            }
            check(contrast(p.on_accent, p.accent) >= 4.5, tag + "primary button label >= 4.5:1");
            check(contrast(p.on_accent, p.accent_hover) >= 4.5, tag + "hovered primary button label >= 4.5:1");
            check(contrast(p.on_accent, p.accent_down) >= 3.0, tag + "pressed primary button label >= 3:1");
            check(p.surface.a < 255 && p.field.a < 255 && p.row_hover.a < 255, tag + "panels stay translucent over pictures");
        }
    }

    // custom accents change only the accent family
    const Palette themed = make_palette(kThemes[0], nullptr);
    const Palette blue = make_palette(kThemes[0], find_accent("blue"));
    check(blue.accent == rgb(0x5b8def) && !(blue.accent == themed.accent), "a custom accent replaces the accent");
    check(blue.window == themed.window && blue.ink == themed.ink && blue.border == themed.border,
          "a custom accent leaves the rest of the theme alone");
    check(make_palette(kThemes[0], find_accent(kThemeAccent)).accent == themed.accent, "the theme accent is the theme's own");

    std::printf("RESULT %d failures\n", failures);
    return failures ? 1 : 0;
}

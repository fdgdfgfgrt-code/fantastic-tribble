// language: C++17, file: frame_zone.hpp, runtime: any (no platform headers), target: tds+ custom window frame
// Pure hit-test geometry for the frameless window. All values are physical pixels in client
// coordinates; the client area is the whole window (the Windows caption and frame are removed).
#pragma once

namespace tds_frame {

enum class Zone {
    Client, Caption,
    Top, Bottom, Left, Right, TopLeft, TopRight, BottomLeft, BottomRight
};

struct Layout {
    int width = 0, height = 0;   // client size
    int border = 6;              // resize grip thickness; ignored when maximised
    int bar_height = 36;         // custom title bar
    int buttons_width = 138;     // minimise / maximise / close, flush right
    bool maximized = false;
};

inline Zone classify(const Layout& l, int x, int y) {
    if (x < 0 || y < 0 || x >= l.width || y >= l.height) return Zone::Client;

    // Caption buttons win over the side grips, so the pixels flush with the right edge still close
    // the window; only the top grip row stays a resize area.
    const bool on_buttons = x >= l.width - l.buttons_width && y >= (l.maximized ? 0 : l.border) && y < l.bar_height;
    if (on_buttons) return Zone::Client;

    if (!l.maximized) {
        const int corner = l.border * 2;
        const bool left = x < l.border, right = x >= l.width - l.border;
        const bool top = y < l.border, bottom = y >= l.height - l.border;
        if ((top && x < corner) || (left && y < corner)) return Zone::TopLeft;
        if ((top && x >= l.width - corner) || (right && y < corner)) return Zone::TopRight;
        if ((bottom && x < corner) || (left && y >= l.height - corner)) return Zone::BottomLeft;
        if ((bottom && x >= l.width - corner) || (right && y >= l.height - corner)) return Zone::BottomRight;
        if (top) return Zone::Top;
        if (bottom) return Zone::Bottom;
        if (left) return Zone::Left;
        if (right) return Zone::Right;
    }

    return y < l.bar_height ? Zone::Caption : Zone::Client;
}

}  // namespace tds_frame

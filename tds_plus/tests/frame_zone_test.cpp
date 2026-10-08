// language: C++17, file: frame_zone_test.cpp, runtime: any, target: tds+ custom window frame geometry
#include <cstdio>

#include "../qt/frame_zone.hpp"

using tds_frame::Layout;
using tds_frame::Zone;
using tds_frame::classify;

static int failures = 0;
#define CHECK(expr)                                                        \
    do {                                                                   \
        if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); ++failures; } \
    } while (0)

int main() {
    Layout l;
    l.width = 560; l.height = 516;

    // title bar: draggable left of the buttons, buttons stay clickable client area
    CHECK(classify(l, 200, 20) == Zone::Caption);
    CHECK(classify(l, 20, 35) == Zone::Caption);
    CHECK(classify(l, 560 - 138 - 1, 20) == Zone::Caption);
    CHECK(classify(l, 560 - 138, 20) == Zone::Client);
    CHECK(classify(l, 559, 20) == Zone::Client);       // flush with the edge: still the close button
    CHECK(classify(l, 559, 6) == Zone::Client);
    CHECK(classify(l, 559, 35) == Zone::Client);
    CHECK(classify(l, 559, 36) == Zone::Right);        // right grip resumes below the bar
    CHECK(classify(l, 200, 36) == Zone::Client);       // first row below the bar
    CHECK(classify(l, 200, 300) == Zone::Client);

    // edges and corners
    CHECK(classify(l, 200, 0) == Zone::Top);
    CHECK(classify(l, 200, 5) == Zone::Top);
    CHECK(classify(l, 200, 6) == Zone::Caption);
    CHECK(classify(l, 0, 200) == Zone::Left);
    CHECK(classify(l, 559, 200) == Zone::Right);
    CHECK(classify(l, 200, 515) == Zone::Bottom);
    CHECK(classify(l, 0, 0) == Zone::TopLeft);
    CHECK(classify(l, 11, 0) == Zone::TopLeft);
    CHECK(classify(l, 0, 11) == Zone::TopLeft);
    CHECK(classify(l, 12, 0) == Zone::Top);
    CHECK(classify(l, 559, 0) == Zone::TopRight);
    CHECK(classify(l, 0, 515) == Zone::BottomLeft);
    CHECK(classify(l, 559, 515) == Zone::BottomRight);
    CHECK(classify(l, 559, 504) == Zone::BottomRight);
    CHECK(classify(l, 559, 503) == Zone::Right);
    CHECK(classify(l, 559, 200) == Zone::Right);
    CHECK(classify(l, 5, 20) == Zone::Left);            // left grip wins over the caption

    // resize grip over the close button's top rows, buttons below that stay clickable
    CHECK(classify(l, 555, 0) == Zone::TopRight);
    CHECK(classify(l, 559, 5) == Zone::TopRight);
    CHECK(classify(l, 500, 2) == Zone::Top);
    CHECK(classify(l, 500, 6) == Zone::Client);

    // maximised: no grips at all, the bar still drags
    l.maximized = true;
    CHECK(classify(l, 0, 0) == Zone::Caption);
    CHECK(classify(l, 200, 0) == Zone::Caption);
    CHECK(classify(l, 559, 515) == Zone::Client);
    CHECK(classify(l, 0, 200) == Zone::Client);
    CHECK(classify(l, 559, 10) == Zone::Client);        // close button reachable at the very edge
    l.maximized = false;

    // scaled layout (150%)
    l.width = 840; l.height = 774; l.border = 9; l.bar_height = 54; l.buttons_width = 207;
    CHECK(classify(l, 400, 30) == Zone::Caption);
    CHECK(classify(l, 840 - 207, 30) == Zone::Client);
    CHECK(classify(l, 839, 30) == Zone::Client);
    CHECK(classify(l, 400, 8) == Zone::Top);
    CHECK(classify(l, 400, 9) == Zone::Caption);
    CHECK(classify(l, 17, 0) == Zone::TopLeft);
    CHECK(classify(l, 18, 0) == Zone::Top);

    // outside the window: never a grip
    CHECK(classify(l, -1, 10) == Zone::Client);
    CHECK(classify(l, 10, 774) == Zone::Client);

    // tiny window: buttons wider than the window must not crash or invert zones
    l = Layout{};
    l.width = 100; l.height = 80; l.buttons_width = 138;
    CHECK(classify(l, 50, 20) == Zone::Client);
    CHECK(classify(l, 50, 0) == Zone::Top);

    std::printf(failures ? "%d FAILED\n" : "all frame zone checks passed\n", failures);
    return failures ? 1 : 0;
}

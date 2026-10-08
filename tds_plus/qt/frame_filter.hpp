// language: C++17, file: frame_filter.hpp, runtime: Qt 6.10/Windows, target: tds+ frameless Qt window
#pragma once

#include "frame_win.hpp"
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QQuickWindow>

// Feeds the window's native messages to tds_frame::handle(). The title bar size is read from
// the QML root (titleBarHeight, captionControlsWidth, both in device-independent pixels).
class FrameFilter final : public QAbstractNativeEventFilter {
public:
    explicit FrameFilter(QQuickWindow* window) : window_(window) {}

    bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override {
        if (event_type != "windows_generic_MSG") return false;
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->hwnd != reinterpret_cast<HWND>(window_->winId())) return false;
        tds_frame::Bar bar;
        bar.height_dp = window_->property("titleBarHeight").toInt();
        bar.buttons_dp = window_->property("captionControlsWidth").toInt();
        LRESULT out = 0;
        if (!tds_frame::handle(msg->hwnd, msg->message, msg->wParam, msg->lParam, &out, bar)) return false;
        *result = static_cast<qintptr>(out);
        return true;
    }

private:
    QQuickWindow* window_;
};

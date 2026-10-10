// language: C++17, file: tray_icon.hpp, runtime: Qt 6.10, target: tds+ notification area icon
// Icon in the Windows notification area (system tray) with a small menu: show/hide the window,
// start/pause autocast, exit. Implemented with Shell_NotifyIcon directly, so no extra Qt module has
// to be shipped. On other platforms it reports itself as unsupported and does nothing.
#pragma once

#include <QObject>
#include <QString>
#include <memory>

class TrayIcon final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)

public:
    explicit TrayIcon(QObject* parent = nullptr);
    ~TrayIcon() override;

    bool supported() const;  // this platform has a notification area
    bool shown() const;      // the icon is in the notification area right now

    bool show();  // returns shown()
    void hide();
    void setToolTip(const QString& text);
    // what the menu offers: "Hide" or "Show", "Pause" or "Start" (greyed out when not possible)
    void setMenuState(bool window_shown, bool running, bool can_toggle_running);

Q_SIGNALS:
    void shownChanged();
    void activated();               // click or Enter on the icon
    void toggleWindowRequested();   // menu: Show / Hide tds+
    void toggleRunningRequested();  // menu: Start / Pause
    void exitRequested();           // menu: Exit

public:
    struct Impl;  // platform part, public only so the window procedure can reach it

private:
    std::unique_ptr<Impl> impl_;
};

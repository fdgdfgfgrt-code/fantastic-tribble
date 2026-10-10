// language: C++17, file: window_control.hpp, runtime: Qt 6.10, target: tds+ desktop window behaviour
// Window behaviour that depends on the settings: hiding to and restoring from the tray, the tray
// icon's visibility, "always on top", and a real exit when closing only hides the window.
#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>

class AppSettings;
class QQuickWindow;
class TrayIcon;

class WindowControl final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool quitting READ quitting CONSTANT)

public:
    WindowControl(AppSettings* settings, TrayIcon* tray, bool custom_frame, QObject* parent = nullptr);
    void setWindow(QQuickWindow* window);
    bool quitting() const { return quitting_; }

    // false when there is no tray icon to come back from: the caller then minimizes normally
    Q_INVOKABLE bool hideToTray();
    Q_INVOKABLE void showFromTray();
    Q_INVOKABLE void toggleFromTray();
    Q_INVOKABLE void activateFromTray();
    Q_INVOKABLE void quit();

private:
    void syncTray();
    void applyAlwaysOnTop();

    AppSettings* settings_;
    TrayIcon* tray_;
    bool custom_frame_;
    QPointer<QQuickWindow> window_;
    bool quitting_ = false;
    bool restore_maximized_ = false;
};

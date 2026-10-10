// language: C++17, file: window_control.cpp, runtime: Qt 6.10 (+ Win32), target: tds+ desktop window behaviour
#include "window_control.hpp"

#include "app_settings.hpp"
#include "tray_icon.hpp"

#include <QCoreApplication>
#include <QQuickWindow>

#ifdef Q_OS_WIN
#include "frame_win.hpp"
#endif

WindowControl::WindowControl(AppSettings* settings, TrayIcon* tray, bool custom_frame, QObject* parent)
    : QObject(parent), settings_(settings), tray_(tray), custom_frame_(custom_frame) {
    connect(settings_, &AppSettings::windowOptionsChanged, this, [this] {
        syncTray();
        applyAlwaysOnTop();
    });
}

void WindowControl::setWindow(QQuickWindow* window) {
    window_ = window;
    if (!window_) return;
    connect(window_, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
        if (visibility == QWindow::Maximized) restore_maximized_ = true;
        else if (visibility == QWindow::Windowed) restore_maximized_ = false;
        syncTray();
    });
    syncTray();
    applyAlwaysOnTop();
}

void WindowControl::syncTray() {
    if (!window_ || !tray_->supported()) return;
    // never leave a hidden window without a way back
    const bool wanted = settings_->minimizeToTray() || settings_->closeToTray() || settings_->startInTray() ||
                        !window_->isVisible();
    if (wanted) tray_->show();
    else tray_->hide();
}

bool WindowControl::hideToTray() {
    if (!window_ || !tray_->show()) return false;
    window_->hide();
    return true;
}

void WindowControl::showFromTray() {
    if (!window_) return;
    if (restore_maximized_) window_->showMaximized();
    else window_->showNormal();
    window_->raise();
    window_->requestActivate();
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(window_->winId());
    if (custom_frame_) tds_frame::install(hwnd);  // idempotent; keeps the frame styles after hide/show
    SetForegroundWindow(hwnd);
#endif
    applyAlwaysOnTop();
    syncTray();
}

void WindowControl::toggleFromTray() {
    if (!window_) return;
    if (window_->isVisible() && window_->visibility() != QWindow::Minimized) hideToTray();
    else showFromTray();
}

void WindowControl::activateFromTray() {
    if (!window_) return;
    // a click on the icon hides a window that is already in front, otherwise it brings it forward
    if (window_->isVisible() && window_->visibility() != QWindow::Minimized && window_->isActive()) hideToTray();
    else showFromTray();
}

void WindowControl::beginQuit() {
    if (quitting_) return;
    quitting_ = true;
    Q_EMIT quittingChanged();
}

void WindowControl::quit() {
    beginQuit();
    QCoreApplication::quit();
}

void WindowControl::applyAlwaysOnTop() {
    if (!window_) return;
#ifdef Q_OS_WIN
    // through Win32 rather than Qt::WindowStaysOnTopHint: changing Qt's window flags would rebuild the
    // window styles and drop the custom frame
    SetWindowPos(reinterpret_cast<HWND>(window_->winId()), settings_->alwaysOnTop() ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
#else
    window_->setFlag(Qt::WindowStaysOnTopHint, settings_->alwaysOnTop());
#endif
}

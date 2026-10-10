// language: C++17, file: tray_icon.cpp, runtime: Qt 6.10 + Win32, target: tds+ notification area icon
#include "tray_icon.hpp"

#include <QtGlobal>

#ifdef Q_OS_WIN

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <QElapsedTimer>
#include <QPointer>
#include <cwchar>

namespace {

constexpr UINT kCallbackMessage = WM_APP + 41;
constexpr UINT kIconId = 1;
constexpr UINT_PTR kMenuToggleWindow = 1, kMenuToggleRunning = 2, kMenuExit = 3;
constexpr int kAppIconResource = 101;  // qt/app.rc

LRESULT CALLBACK tray_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

}  // namespace

struct TrayIcon::Impl {
    TrayIcon* owner = nullptr;
    HWND hwnd = nullptr;
    HICON icon = nullptr;
    bool owns_icon = false;
    bool shown = false;
    UINT taskbar_created = 0;
    QString tooltip = QStringLiteral("tds+");
    bool window_shown = true, running = false, can_toggle = false;
    QElapsedTimer last_activation;

    NOTIFYICONDATAW data(UINT flags) const {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = kIconId;
        nid.uFlags = flags;
        nid.uCallbackMessage = kCallbackMessage;
        nid.hIcon = icon;
        const std::wstring tip = tooltip.left(127).toStdWString();
        wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
        return nid;
    }

    bool add() {
        NOTIFYICONDATAW nid = data(NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP);
        if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
            qWarning("tray: the icon could not be added (error %lu); minimizing and closing stay normal",
                     GetLastError());
            return false;
        }
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        return true;
    }

    void activate() {
        // Enter on the icon can arrive twice; one click must not toggle the window twice
        if (last_activation.isValid() && last_activation.elapsed() < 300) return;
        last_activation.restart();
        Q_EMIT owner->activated();
    }

    void menu(int x, int y) {
        HMENU popup = CreatePopupMenu();
        if (!popup) return;
        AppendMenuW(popup, MF_STRING, kMenuToggleWindow, window_shown ? L"Hide tds+" : L"Show tds+");
        AppendMenuW(popup, MF_STRING | (can_toggle ? 0 : MF_GRAYED), kMenuToggleRunning,
                    running ? L"Pause\tF6" : L"Start\tF6");
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_STRING, kMenuExit, L"Exit");
        SetMenuDefaultItem(popup, static_cast<UINT>(kMenuToggleWindow), FALSE);
        // the menu only closes on an outside click when its owner is in the foreground
        SetForegroundWindow(hwnd);
        const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        const UINT command = static_cast<UINT>(TrackPopupMenuEx(
            popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, x, y, hwnd, nullptr));
        PostMessageW(hwnd, WM_NULL, 0, 0);
        DestroyMenu(popup);
        QPointer<TrayIcon> alive(owner);
        if (command == kMenuToggleWindow) Q_EMIT owner->toggleWindowRequested();
        else if (command == kMenuToggleRunning && can_toggle) Q_EMIT owner->toggleRunningRequested();
        else if (command == kMenuExit && alive) Q_EMIT owner->exitRequested();
    }
};

namespace {

LRESULT CALLBACK tray_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* impl = reinterpret_cast<TrayIcon::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (impl && msg == kCallbackMessage) {
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            impl->activate();
            break;
        case WM_CONTEXTMENU:
            impl->menu(GET_X_LPARAM(wp), GET_Y_LPARAM(wp));
            break;
        default:
            break;
        }
        return 0;
    }
    if (impl && impl->taskbar_created && msg == impl->taskbar_created) {
        // Explorer restarted and forgot every icon
        if (impl->shown) impl->shown = impl->add();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

TrayIcon::TrayIcon(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = tray_window_proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"tds_plus_tray";
    RegisterClassExW(&cls);  // fails harmlessly when the class already exists
    // a hidden top-level window rather than a message-only one: the menu needs a window that can be
    // put in the foreground
    impl_->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"tds+ tray", WS_POPUP, 0, 0, 0, 0,
                                  nullptr, nullptr, instance, nullptr);
    if (impl_->hwnd) SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_.get()));
    impl_->icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kAppIconResource), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                LR_DEFAULTCOLOR));
    impl_->owns_icon = impl_->icon != nullptr;
    if (!impl_->icon) impl_->icon = LoadIconW(nullptr, IDI_APPLICATION);
}

TrayIcon::~TrayIcon() {
    hide();
    if (impl_->hwnd) {
        SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(impl_->hwnd);
    }
    if (impl_->owns_icon) DestroyIcon(impl_->icon);
}

bool TrayIcon::supported() const { return impl_->hwnd != nullptr; }
bool TrayIcon::shown() const { return impl_->shown; }

bool TrayIcon::show() {
    if (impl_->shown || !impl_->hwnd) return impl_->shown;
    impl_->shown = impl_->add();
    if (impl_->shown) Q_EMIT shownChanged();
    return impl_->shown;
}

void TrayIcon::hide() {
    if (!impl_->shown) return;
    NOTIFYICONDATAW nid = impl_->data(0);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    impl_->shown = false;
    Q_EMIT shownChanged();
}

void TrayIcon::setToolTip(const QString& text) {
    if (impl_->tooltip == text) return;
    impl_->tooltip = text;
    if (!impl_->shown) return;
    NOTIFYICONDATAW nid = impl_->data(NIF_TIP | NIF_SHOWTIP);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::setMenuState(bool window_shown, bool running, bool can_toggle_running) {
    impl_->window_shown = window_shown;
    impl_->running = running;
    impl_->can_toggle = can_toggle_running;
}

#else  // no notification area support outside Windows

struct TrayIcon::Impl {};

TrayIcon::TrayIcon(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}
TrayIcon::~TrayIcon() = default;
bool TrayIcon::supported() const { return false; }
bool TrayIcon::shown() const { return false; }
bool TrayIcon::show() { return false; }
void TrayIcon::hide() {}
void TrayIcon::setToolTip(const QString&) {}
void TrayIcon::setMenuState(bool, bool, bool) {}

#endif

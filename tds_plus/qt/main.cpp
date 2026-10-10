// language: C++17, file: main.cpp, runtime: Qt 6.10/MSVC, target: Windows 11 GUI subsystem
#include <windows.h>
#include <dwmapi.h>
#include "app_settings.hpp"
#include "control_model.hpp"
#include "frame_filter.hpp"
#include "tray_icon.hpp"
#include "window_control.hpp"
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <cstdio>
#include <memory>
#include <thread>
#ifdef TDS_UI_SMOKE
#include "smoke_test.hpp"
#endif

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("tds+");
    app.setOrganizationName("tds-plus");
    app.setWindowIcon(QIcon("qrc:/qt/tds_plus.ico"));
    for (const auto* font_path : {":/assets/fonts/GeistMono-Medium.ttf",
                                 ":/assets/fonts/GeistMono-SemiBold.ttf"}) {
        if (QFontDatabase::addApplicationFont(font_path) < 0) {
            MessageBoxW(nullptr, L"The bundled interface font could not be loaded.",
                        L"tds+", MB_OK | MB_ICONERROR);
            return 2;
        }
    }
    QFont ui_font("Geist Mono");
    ui_font.setPixelSize(14);
    ui_font.setWeight(QFont::Medium);
    ui_font.setHintingPreference(QFont::PreferVerticalHinting);
    app.setFont(ui_font);
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"offline", "Open the UI without attaching to Roblox."});
    parser.addOption({"native-frame", "Use the standard Windows title bar instead of the custom one."});
    parser.addOption({"config-dir", "Directory containing the existing tds+ INI files.", "path"});
    parser.process(app);

    QString config_dir = parser.value("config-dir");
    if (config_dir.isEmpty()) {
        config_dir = QCoreApplication::applicationDirPath();
        if (!QFileInfo::exists(config_dir + "/tds_wiki_db.ini"))
            config_dir = QDir(config_dir).absoluteFilePath("..");
    }
    if (!QDir::setCurrent(config_dir)) return 2;
    if (!QFileInfo::exists("tds_wiki_db.ini")) {
        MessageBoxW(nullptr, L"tds_wiki_db.ini was not found. Open the app from the tds_plus folder.",
                    L"tds+", MB_OK | MB_ICONERROR);
        return 2;
    }
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        if (FILE* file = fopen("tds_qt.log", "a")) {
            fprintf(file, "%s\n", message.toUtf8().constData());
            fclose(file);
        }
    });

    const bool offline = parser.isSet("offline");
    const bool custom_frame = !parser.isSet("native-frame");

#ifdef TDS_UI_SMOKE
    // the UI test starts from default settings and never touches the user's own
    const QString settings_file = QDir(config_dir).absoluteFilePath("smoke_ui_settings.ini");
    QFile::remove(settings_file);
    QSettings ui_store(settings_file, QSettings::IniFormat);
    const QString background_dir = QDir(config_dir).absoluteFilePath("smoke_backgrounds");
    QDir(background_dir).removeRecursively();
#else
    QSettings ui_store;
    const QString background_dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backgrounds");
#endif
    AppSettings app_settings(&ui_store, background_dir);
    TrayIcon tray_icon;
    WindowControl window_control(&app_settings, &tray_icon, custom_frame);
    bool start_hidden = false;
#ifndef TDS_UI_SMOKE
    // only start hidden when there really is a tray icon to come back from
    if (app_settings.startInTray()) start_hidden = tray_icon.show();
#endif

    std::thread engine_thread([offline] { tds_run_engine(offline); });
    ControlModel control_model(offline);
    QQmlApplicationEngine qml_engine;
    BOOL motion_enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motion_enabled, 0);
    qml_engine.rootContext()->setContextProperty("systemMotionEnabled", motion_enabled != FALSE);
    qml_engine.rootContext()->setContextProperty("controlModel", &control_model);
    qml_engine.rootContext()->setContextProperty("customFrameEnabled", custom_frame);
    qml_engine.rootContext()->setContextProperty("appSettings", &app_settings);
    qml_engine.rootContext()->setContextProperty("trayIcon", &tray_icon);
    qml_engine.rootContext()->setContextProperty("windowControl", &window_control);
    QObject::connect(&control_model, &ControlModel::exitRequested, &app, &QCoreApplication::quit);
    qml_engine.load(QUrl("qrc:/qml/Main.qml"));
    if (qml_engine.rootObjects().isEmpty()) {
        tds_request_exit();
        engine_thread.join();
        return 3;
    }
    auto* window = qobject_cast<QQuickWindow*>(qml_engine.rootObjects().front());
    std::unique_ptr<FrameFilter> frame_filter;  // must live as long as the event loop
    if (window) {
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        // dark or light system parts (native frame, menus) follow the theme
        const auto apply_dark_mode = [hwnd, &app_settings] {
            const BOOL dark = app_settings.palette().value("dark").toBool() ? TRUE : FALSE;
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        };
        apply_dark_mode();
        QObject::connect(&app_settings, &AppSettings::paletteChanged, window, apply_dark_mode);
        if (custom_frame) {
            frame_filter = std::make_unique<FrameFilter>(window);
            app.installNativeEventFilter(frame_filter.get());
            tds_frame::install(hwnd);
            // The frameless window relies on Qt agreeing with Windows about the client size; say so in the log if not.
            QTimer::singleShot(800, window, [window, hwnd] {
                if (!window->isVisible()) return;
                RECT client{};
                if (!GetClientRect(hwnd, &client)) return;
                const qreal ratio = window->devicePixelRatio();
                if (qAbs(client.right - qRound(window->width() * ratio)) > 2 ||
                    qAbs(client.bottom - qRound(window->height() * ratio)) > 2)
                    qWarning("custom frame: Qt window is %dx%d, client area is %ldx%ld (use --native-frame)",
                             window->width(), window->height(), client.right, client.bottom);
            });
        }

        // tray icon: menu state, tooltip and actions
        const auto sync_tray = [window, &tray_icon, &control_model] {
            tray_icon.setMenuState(window->isVisible() && window->visibility() != QWindow::Minimized,
                                   control_model.running(), control_model.ready());
            tray_icon.setToolTip(QStringLiteral("tds+ · ") + (control_model.running() ? QStringLiteral("Running")
                                                                                     : QStringLiteral("Paused")) +
                                 QStringLiteral(" · ") + control_model.phase());
        };
        sync_tray();
        QObject::connect(&control_model, &ControlModel::stateChanged, window, sync_tray);
        QObject::connect(window, &QWindow::visibilityChanged, window, sync_tray);
        QObject::connect(&tray_icon, &TrayIcon::activated, &window_control, &WindowControl::activateFromTray);
        QObject::connect(&tray_icon, &TrayIcon::toggleWindowRequested, &window_control, &WindowControl::toggleFromTray);
        QObject::connect(&tray_icon, &TrayIcon::toggleRunningRequested, &control_model, &ControlModel::toggle_running);
        QObject::connect(&tray_icon, &TrayIcon::exitRequested, &window_control, &WindowControl::quit);

#ifndef TDS_UI_SMOKE
        QSettings settings;
        QSize size = settings.value("window/size", window->size()).toSize();
        if (settings.value("window/layoutVersion", 0).toInt() < 1) {
            size = window->size();
            settings.setValue("window/layoutVersion", 1);
        }
        size = size.expandedTo(window->minimumSize());
        window->resize(size);
        settings.setValue("window/size", size);
        window->setProperty("saved_window_size", size);
        auto* resize_timer = new QTimer(window);
        resize_timer->setSingleShot(true);
        const auto capture_size = [window, resize_timer] {
            if (window->visibility() != QWindow::Windowed) return;
            window->setProperty("saved_window_size", window->size());
            resize_timer->start(300);
        };
        const auto save_size = [window] {
            QSettings().setValue("window/size", window->property("saved_window_size"));
        };
        QObject::connect(window, &QWindow::widthChanged, window, capture_size);
        QObject::connect(window, &QWindow::heightChanged, window, capture_size);
        QObject::connect(resize_timer, &QTimer::timeout, window, save_size);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, window, save_size);
#endif
        window_control.setWindow(window);
        if (!start_hidden) window->show();
#ifdef TDS_UI_SMOKE
        QTimer::singleShot(650, &app, [&app, window, &control_model, config_dir, &app_settings, &tray_icon, &window_control] {
            app.exit(control_model.offline()
                         ? run_smoke_test(window, control_model, config_dir, app_settings, tray_icon, window_control)
                         : run_attach_test(window, control_model, config_dir));
        });
#endif
    }
    const int result = app.exec();
    tray_icon.hide();
    tds_request_exit();
    engine_thread.join();
    return result;
}

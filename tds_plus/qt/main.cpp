// language: C++17, file: main.cpp, runtime: Qt 6.10/MSVC, target: Windows 11 GUI subsystem
#include <windows.h>
#include <dwmapi.h>
#include "control_model.hpp"
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <cstdio>
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
    std::thread engine_thread([offline] { tds_run_engine(offline); });
    ControlModel control_model(offline);
    QQmlApplicationEngine qml_engine;
    BOOL motion_enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motion_enabled, 0);
    qml_engine.rootContext()->setContextProperty("systemMotionEnabled", motion_enabled != FALSE);
    qml_engine.rootContext()->setContextProperty("controlModel", &control_model);
    QObject::connect(&control_model, &ControlModel::exitRequested, &app, &QCoreApplication::quit);
    qml_engine.load(QUrl("qrc:/qml/Main.qml"));
    if (qml_engine.rootObjects().isEmpty()) {
        tds_request_exit();
        engine_thread.join();
        return 3;
    }
    auto* window = qobject_cast<QQuickWindow*>(qml_engine.rootObjects().front());
    if (window) {
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), 20, &dark, sizeof(dark));
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
#else
        QTimer::singleShot(650, &app, [&app, window, &control_model, config_dir] {
            app.exit(control_model.offline() ? run_smoke_test(window, control_model, config_dir)
                                            : run_attach_test(window, control_model, config_dir));
        });
#endif
    }
    const int result = app.exec();
    tds_request_exit();
    engine_thread.join();
    return result;
}

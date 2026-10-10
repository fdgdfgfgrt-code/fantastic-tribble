// language: C++17, file: app_settings_test.cpp, runtime: Qt 6.10, target: tds+ UI preferences
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QVector3D>
#include <cstdio>

#include "../qt/app_settings.hpp"
#include "test_images.hpp"

static int failures = 0;
static void check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    failures += !ok;
}

static QString write_image(const QString& path, int width, int height, bool alpha, const char* format) {
    QImage image(width, height, alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    image.fill(alpha ? QColor(20, 40, 200, 128) : QColor(20, 40, 200));
    QPainter painter(&image);
    painter.fillRect(0, 0, width / 2, height, QColor(220, 80, 40));
    painter.end();
    image.save(path, format);
    return path;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);  // loads the image format plugins (GIF, JPEG)
    QTemporaryDir temp;
    if (!temp.isValid()) return 2;
    const QString ini = temp.filePath("settings.ini");
    const QString store_dir = temp.filePath("backgrounds");

    // ---- defaults ----
    {
        QSettings store(ini, QSettings::IniFormat);
        AppSettings s(&store, store_dir);
        check(s.theme() == "graphite" && s.accent() == "theme", "defaults: graphite theme with its own accent");
        check(s.backgroundMode() == "waves" && !s.hasBackgroundFile() && s.backgroundDim() == AppSettings::kDefaultDim,
              "defaults: animated waves, no picture, default darkening");
        check(!s.minimizeToTray() && !s.closeToTray() && !s.startInTray() && !s.alwaysOnTop(),
              "defaults: every window option is off");
        check(s.lastError().isEmpty(), "defaults: no error");
        const QVariantMap p = s.palette();
        check(p.value("window").value<QColor>() == QColor("#09090b") && p.value("ink").value<QColor>() == QColor("#eaeaec"),
              "palette: graphite window and text colors");
        const char* keys[] = {"window", "surface", "field", "rowHover", "popup", "popupBorder", "raised", "borderStrong",
                              "control", "thumb", "controlHover", "controlDown", "input", "focus", "border", "divider",
                              "separator", "ink", "muted", "dim", "accent", "accentHover", "accentDown", "onAccent",
                              "onAccentDim", "danger", "scroll", "scrollActive", "selection"};
        bool all = true;
        for (const char* key : keys) all = all && p.value(key).value<QColor>().isValid();
        check(all, "palette: every color the QML uses is present");
        check(p.value("waveTint").value<QVector3D>() == QVector3D(1.0f, 1.0f, 1.035f) && p.value("dark").toBool(),
              "palette: wave colors and the dark flag");
        check(s.themes().size() == 4 && s.accents().size() == 6, "four themes and six accents are offered");
        check(p.value("surface").value<QColor>().alpha() < 255, "the list panel stays translucent");
    }

    // ---- changes persist, notifications fire ----
    {
        QSettings store(ini, QSettings::IniFormat);
        AppSettings s(&store, store_dir);
        int palette_signals = 0, window_signals = 0;
        QObject::connect(&s, &AppSettings::paletteChanged, [&] { ++palette_signals; });
        QObject::connect(&s, &AppSettings::windowOptionsChanged, [&] { ++window_signals; });
        s.setTheme("paper");
        s.setAccent("rose");
        s.setBackgroundDim(70);
        s.setMinimizeToTray(true);
        s.setCloseToTray(true);
        s.setStartInTray(true);
        s.setAlwaysOnTop(true);
        s.setTheme("paper");  // unchanged: no signal
        check(palette_signals == 2 && window_signals == 4, "one notification per real change");
        check(s.palette().value("window").value<QColor>() == QColor("#f3f3f1"), "the palette follows the theme");
        check(s.palette().value("accent").value<QColor>() == QColor("#ec6f8e"), "the palette follows the accent");
        bool theme_accent_follows = false;
        for (const QVariant& a : s.accents()) {
            const QVariantMap m = a.toMap();
            if (m.value("id") == "theme") theme_accent_follows = m.value("color").value<QColor>() == QColor("#1d1d22");
        }
        check(theme_accent_follows, "the 'theme' accent swatch shows the current theme's accent");
    }
    {
        QSettings store(ini, QSettings::IniFormat);
        AppSettings s(&store, store_dir);
        check(s.theme() == "paper" && s.accent() == "rose" && s.backgroundDim() == 70, "appearance survives a restart");
        check(s.minimizeToTray() && s.closeToTray() && s.startInTray() && s.alwaysOnTop(), "window options survive a restart");
    }

    // ---- invalid input is refused or repaired ----
    {
        QSettings store(ini, QSettings::IniFormat);
        store.setValue("ui/theme", "neon");
        store.setValue("ui/accent", "plaid");
        store.setValue("ui/background/mode", "video");
        store.setValue("ui/background/dim", 400);
        store.sync();
        AppSettings s(&store, store_dir);
        check(s.theme() == "graphite" && s.accent() == "theme" && s.backgroundMode() == "waves",
              "unknown stored values fall back to the defaults");
        check(s.backgroundDim() == AppSettings::kMaxDim, "a stored darkening above the maximum is clamped");
        s.setTheme("neon");
        s.setAccent("plaid");
        s.setBackgroundMode("video");
        s.setBackgroundDim(-5);
        check(s.theme() == "graphite" && s.accent() == "theme" && s.backgroundMode() == "waves" && s.backgroundDim() == 0,
              "unknown values from the UI are ignored and the darkening is clamped");
        s.setBackgroundMode("picture");
        check(s.backgroundMode() == "waves" && !s.lastError().isEmpty(), "picture mode needs a picture");
        s.setBackgroundMode("solid");
        check(s.backgroundMode() == "solid", "solid mode is accepted");
    }

    // ---- importing pictures ----
    QSettings store(temp.filePath("import.ini"), QSettings::IniFormat);
    AppSettings s(&store, store_dir);
    int background_signals = 0;
    QObject::connect(&s, &AppSettings::backgroundChanged, [&] { ++background_signals; });

    const QString big = write_image(temp.filePath("Big Photo.png"), 4000, 1000, false, "PNG");
    check(s.importBackground(QUrl::fromLocalFile(big)), "a large photo is imported");
    check(s.backgroundMode() == "picture" && s.hasBackgroundFile() && !s.backgroundAnimated() && background_signals == 1,
          "the imported photo becomes the background");
    check(s.backgroundName() == "Big Photo.png", "the original file name is shown");
    check(QFileInfo(s.backgroundPath()).absolutePath() == QDir::cleanPath(store_dir), "the copy lives in the app's folder");
    const QSize stored = QImageReader(s.backgroundPath()).size();
    check(stored == QSize(2560, 640), "a large photo is shrunk to 2560 on the long side, keeping its shape");
    check(s.backgroundPath().endsWith(".jpg"), "an opaque picture is stored as JPEG");
    check(s.backgroundUrl().isLocalFile() && s.backgroundUrl().toLocalFile() == s.backgroundPath(), "the URL points at the copy");
    QFile::remove(big);
    check(QFileInfo(s.backgroundPath()).isFile(), "deleting the original does not affect the background");
    const QString first_copy = s.backgroundPath();

    const QString transparent = write_image(temp.filePath("logo.png"), 300, 200, true, "PNG");
    check(s.importBackground(QUrl::fromLocalFile(transparent)), "a transparent picture is imported");
    check(s.backgroundPath().endsWith(".png") && QImageReader(s.backgroundPath()).size() == QSize(300, 200),
          "a transparent picture keeps PNG and its size");
    check(!QFileInfo(first_copy).exists(), "the previous copy is deleted");

    QFile gif(temp.filePath("loop.gif"));
    gif.open(QIODevice::WriteOnly);
    gif.write(reinterpret_cast<const char*>(kTwoFrameGif), sizeof(kTwoFrameGif));
    gif.close();
    check(s.importBackground(QUrl::fromLocalFile(gif.fileName())), "an animated GIF is imported");
    check(s.backgroundAnimated() && s.backgroundPath().endsWith(".gif"), "an animated GIF stays an animated GIF");
    QImageReader copied(s.backgroundPath());
    check(copied.imageCount() == 2, "the copied GIF keeps both frames");

    const QString gif_copy = s.backgroundPath();
    QFile text(temp.filePath("notes.png"));
    text.open(QIODevice::WriteOnly);
    text.write("this is not a picture");
    text.close();
    check(!s.importBackground(QUrl::fromLocalFile(text.fileName())) && !s.lastError().isEmpty(),
          "a file that is not a picture is refused with a reason");
    check(s.backgroundPath() == gif_copy && s.backgroundMode() == "picture", "a refused file leaves the background alone");
    check(!s.importBackground(QUrl::fromLocalFile(temp.filePath("missing.png"))), "a missing file is refused");
    check(!s.importBackground(QUrl("https://example.org/a.png")), "a web address is refused");
    QFile huge(temp.filePath("huge.png"));
    huge.open(QIODevice::WriteOnly);
    huge.resize(AppSettings::kMaxFileBytes + 1);
    huge.close();
    check(!s.importBackground(QUrl::fromLocalFile(huge.fileName())) && s.lastError().contains("40 MB"),
          "a file over the size limit is refused before it is read");
    s.importBackground(QUrl::fromLocalFile(transparent));
    check(s.lastError().isEmpty(), "a successful import clears the last error");

    // ---- restart with the picture, then with the picture gone ----
    {
        AppSettings again(&store, store_dir);
        check(again.backgroundMode() == "picture" && again.backgroundPath() == s.backgroundPath(), "the picture survives a restart");
    }
    QFile::remove(s.backgroundPath());
    {
        AppSettings again(&store, store_dir);
        check(again.backgroundMode() == "waves" && !again.hasBackgroundFile() && !again.lastError().isEmpty(),
              "a deleted picture falls back to the waves with an explanation");
    }
    store.setValue("ui/background/file", transparent);  // a file outside the app's folder
    store.setValue("ui/background/mode", "picture");
    {
        AppSettings again(&store, store_dir);
        check(again.backgroundMode() == "waves" && !again.hasBackgroundFile(), "a stored path outside the app's folder is not trusted");
        check(QFileInfo(transparent).exists(), "and that outside file is not deleted");
    }

    // ---- clear and reset ----
    AppSettings fresh(&store, store_dir);
    fresh.importBackground(QUrl::fromLocalFile(transparent));
    const QString copy = fresh.backgroundPath();
    fresh.clearBackground();
    check(fresh.backgroundMode() == "waves" && !fresh.hasBackgroundFile() && !QFileInfo(copy).exists(),
          "removing the picture deletes the copy and goes back to the waves");
    fresh.setTheme("moss");
    fresh.setAccent("amber");
    fresh.setBackgroundDim(10);
    fresh.setCloseToTray(true);
    fresh.resetAppearance();
    check(fresh.theme() == "graphite" && fresh.accent() == "theme" && fresh.backgroundMode() == "waves" &&
              fresh.backgroundDim() == AppSettings::kDefaultDim,
          "reset restores the default look");
    check(fresh.closeToTray(), "reset does not touch the window options");

    std::printf("RESULT %d failures\n", failures);
    return failures ? 1 : 0;
}

// language: C++17, file: app_settings.cpp, runtime: Qt 6.10, target: tds+ desktop UI
#include "app_settings.hpp"

#include "themes.hpp"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>
#include <QVector3D>
#include <algorithm>

namespace {

constexpr const char* kModeWaves = "waves";
constexpr const char* kModeSolid = "solid";
constexpr const char* kModePicture = "picture";

QColor to_qcolor(tds_theme::Rgba c) { return QColor(c.r, c.g, c.b, c.a); }

QVector3D to_vector(const float (&v)[3]) { return QVector3D(v[0], v[1], v[2]); }

bool valid_mode(const QString& mode) {
    return mode == QLatin1String(kModeWaves) || mode == QLatin1String(kModeSolid) || mode == QLatin1String(kModePicture);
}

const tds_theme::ThemeDef& theme_or_default(const QString& id) {
    const tds_theme::ThemeDef* t = tds_theme::find_theme(id.toUtf8().constData());
    return t ? *t : tds_theme::kThemes[0];
}

}  // namespace

AppSettings::AppSettings(QSettings* store, const QString& storage_dir, QObject* parent)
    : QObject(parent), store_(store), storage_dir_(QDir::cleanPath(storage_dir)) {
    load();
}

void AppSettings::load() {
    const QString theme = store_->value("ui/theme", tds_theme::kDefaultTheme).toString();
    theme_ = tds_theme::find_theme(theme.toUtf8().constData()) ? theme : QString(tds_theme::kDefaultTheme);
    const QString accent = store_->value("ui/accent", tds_theme::kThemeAccent).toString();
    accent_ = tds_theme::find_accent(accent.toUtf8().constData()) ? accent : QString(tds_theme::kThemeAccent);

    const QString mode = store_->value("ui/background/mode", kModeWaves).toString();
    background_mode_ = valid_mode(mode) ? mode : QString(kModeWaves);
    background_file_ = store_->value("ui/background/file").toString();
    background_name_ = store_->value("ui/background/name").toString();
    background_animated_ = store_->value("ui/background/animated", false).toBool();
    bool dim_ok = false;
    const int dim = store_->value("ui/background/dim", kDefaultDim).toInt(&dim_ok);
    background_dim_ = dim_ok ? std::clamp(dim, 0, kMaxDim) : kDefaultDim;

    // the picture must still be our own readable copy; otherwise forget it rather than show nothing
    if (!background_file_.isEmpty() &&
        (!ownsFile(background_file_) || !QFileInfo(background_file_).isFile() || !QImageReader(background_file_).canRead())) {
        forgetBackgroundFile();
        if (background_mode_ == QLatin1String(kModePicture))
            last_error_ = QStringLiteral("The background picture is missing, so the animated background is back.");
    }
    if (background_mode_ == QLatin1String(kModePicture) && background_file_.isEmpty()) background_mode_ = kModeWaves;

    minimize_to_tray_ = store_->value("ui/window/minimizeToTray", false).toBool();
    close_to_tray_ = store_->value("ui/window/closeToTray", false).toBool();
    start_in_tray_ = store_->value("ui/window/startInTray", false).toBool();
    always_on_top_ = store_->value("ui/window/alwaysOnTop", false).toBool();
}

QVariantMap AppSettings::palette() const {
    const tds_theme::Palette p = tds_theme::make_palette(theme_or_default(theme_),
                                                         tds_theme::find_accent(accent_.toUtf8().constData()));
    QVariantMap map;
    const struct { const char* key; tds_theme::Rgba color; } colors[] = {
        {"window", p.window},           {"surface", p.surface},         {"field", p.field},
        {"rowHover", p.row_hover},      {"popup", p.popup},             {"popupBorder", p.popup_border},
        {"raised", p.raised},           {"borderStrong", p.border_strong}, {"control", p.control},
        {"thumb", p.thumb},             {"controlHover", p.control_hover}, {"controlDown", p.control_down},
        {"input", p.input},             {"focus", p.focus},             {"border", p.border},
        {"divider", p.divider},         {"separator", p.separator},     {"ink", p.ink},
        {"muted", p.muted},             {"dim", p.dim},                 {"accent", p.accent},
        {"accentHover", p.accent_hover}, {"accentDown", p.accent_down}, {"onAccent", p.on_accent},
        {"onAccentDim", p.on_accent_dim}, {"danger", p.danger},         {"scroll", p.scroll},
        {"scrollActive", p.scroll_active}, {"selection", p.selection},
    };
    for (const auto& c : colors) map.insert(QString::fromLatin1(c.key), to_qcolor(c.color));
    map.insert(QStringLiteral("waveBase"), to_vector(p.wave_base));
    map.insert(QStringLiteral("waveTint"), to_vector(p.wave_tint));
    map.insert(QStringLiteral("dark"), p.dark);
    return map;
}

QVariantList AppSettings::themes() const {
    QVariantList list;
    for (const tds_theme::ThemeDef& t : tds_theme::kThemes) {
        const tds_theme::Palette p = tds_theme::make_palette(t, nullptr);
        list.append(QVariantMap{{"id", QString::fromLatin1(t.id)},
                                {"name", QString::fromLatin1(t.name)},
                                {"dark", t.dark},
                                {"window", to_qcolor(p.window)},
                                {"panel", to_qcolor(tds_theme::over(p.surface, p.window))},
                                {"border", to_qcolor(p.border)},
                                {"ink", to_qcolor(p.ink)},
                                {"muted", to_qcolor(p.muted)},
                                {"accent", to_qcolor(p.accent)}});
    }
    return list;
}

QVariantList AppSettings::accents() const {
    const tds_theme::ThemeDef& theme = theme_or_default(theme_);
    QVariantList list;
    for (const tds_theme::AccentDef& a : tds_theme::kAccents) {
        const tds_theme::Palette p = tds_theme::make_palette(theme, &a);
        list.append(QVariantMap{{"id", QString::fromLatin1(a.id)},
                                {"name", QString::fromLatin1(a.name)},
                                {"color", to_qcolor(p.accent)}});
    }
    return list;
}

QUrl AppSettings::backgroundUrl() const {
    return background_file_.isEmpty() ? QUrl() : QUrl::fromLocalFile(background_file_);
}

void AppSettings::setTheme(const QString& id) {
    if (id == theme_ || !tds_theme::find_theme(id.toUtf8().constData())) return;
    theme_ = id;
    store_->setValue("ui/theme", theme_);
    Q_EMIT themeChanged();
    Q_EMIT paletteChanged();
}

void AppSettings::setAccent(const QString& id) {
    if (id == accent_ || !tds_theme::find_accent(id.toUtf8().constData())) return;
    accent_ = id;
    store_->setValue("ui/accent", accent_);
    Q_EMIT accentChanged();
    Q_EMIT paletteChanged();
}

void AppSettings::setBackgroundMode(const QString& mode) {
    if (mode == background_mode_ || !valid_mode(mode)) return;
    if (mode == QLatin1String(kModePicture) && background_file_.isEmpty()) {
        fail(QStringLiteral("Choose a picture first."));
        return;
    }
    background_mode_ = mode;
    store_->setValue("ui/background/mode", background_mode_);
    Q_EMIT backgroundChanged();
}

void AppSettings::setBackgroundDim(int percent) {
    percent = std::clamp(percent, 0, kMaxDim);
    if (percent == background_dim_) return;
    background_dim_ = percent;
    store_->setValue("ui/background/dim", background_dim_);
    Q_EMIT backgroundDimChanged();
}

void AppSettings::setWindowOption(bool& field, const char* key, bool on) {
    if (field == on) return;
    field = on;
    store_->setValue(QString::fromLatin1(key), on);
    Q_EMIT windowOptionsChanged();
}

void AppSettings::setMinimizeToTray(bool on) { setWindowOption(minimize_to_tray_, "ui/window/minimizeToTray", on); }
void AppSettings::setCloseToTray(bool on) { setWindowOption(close_to_tray_, "ui/window/closeToTray", on); }
void AppSettings::setStartInTray(bool on) { setWindowOption(start_in_tray_, "ui/window/startInTray", on); }
void AppSettings::setAlwaysOnTop(bool on) { setWindowOption(always_on_top_, "ui/window/alwaysOnTop", on); }

void AppSettings::fail(const QString& message) {
    last_error_ = message;
    Q_EMIT lastErrorChanged();
}

void AppSettings::clearError() {
    if (last_error_.isEmpty()) return;
    last_error_.clear();
    Q_EMIT lastErrorChanged();
}

bool AppSettings::ownsFile(const QString& path) const {
    const QString dir = QFileInfo(QDir::cleanPath(path)).absolutePath();
    return !storage_dir_.isEmpty() && QDir::cleanPath(dir).compare(storage_dir_, Qt::CaseInsensitive) == 0;
}

void AppSettings::forgetBackgroundFile() {
    background_file_.clear();
    background_name_.clear();
    background_animated_ = false;
    store_->remove("ui/background/file");
    store_->remove("ui/background/name");
    store_->remove("ui/background/animated");
}

bool AppSettings::importBackground(const QUrl& file) {
    clearError();
    if (!file.isLocalFile()) {
        fail(QStringLiteral("Choose a file on this computer."));
        return false;
    }
    const QString source = file.toLocalFile();
    const QFileInfo info(source);
    if (!info.isFile()) {
        fail(QStringLiteral("The file does not exist."));
        return false;
    }
    if (info.size() > kMaxFileBytes) {
        fail(QStringLiteral("The file is larger than %1 MB.").arg(kMaxFileBytes / (1024 * 1024)));
        return false;
    }

    QImageReader reader(source);
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format().toLower();
    const bool known_format = format == "png" || format == "jpeg" || format == "jpg" || format == "bmp" ||
                              format == "gif" || format == "webp";
    if (!reader.canRead() || !known_format) {
        fail(QStringLiteral("This file is not a picture tds+ can show (PNG, JPG, BMP or GIF)."));
        return false;
    }
    const QSize size = reader.size();
    if (!size.isValid() || size.isEmpty()) {
        fail(QStringLiteral("The picture has no size; it may be damaged."));
        return false;
    }

    if (!QDir().mkpath(storage_dir_)) {
        fail(QStringLiteral("The settings folder cannot be written: %1").arg(QDir::toNativeSeparators(storage_dir_)));
        return false;
    }
    static int sequence = 0;
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch()) + QLatin1Char('-') + QString::number(++sequence);

    const bool animated = reader.supportsAnimation() && reader.imageCount() > 1;
    QString target;
    if (animated) {
        if (std::max(size.width(), size.height()) > kMaxAnimatedSide) {
            fail(QStringLiteral("The animation is larger than %1 pixels.").arg(kMaxAnimatedSide));
            return false;
        }
        // an animation is kept as it is: re-encoding would need a GIF writer, which Qt does not have
        target = storage_dir_ + QStringLiteral("/background-") + stamp + QLatin1Char('.') + QString::fromLatin1(format);
        if (!QFile::copy(source, target)) {
            fail(QStringLiteral("The picture could not be copied to the settings folder."));
            return false;
        }
    } else {
        reader.setAutoTransform(true);  // honour the EXIF orientation of photos
        if (std::max(size.width(), size.height()) > kMaxStillSide)
            reader.setScaledSize(size.scaled(kMaxStillSide, kMaxStillSide, Qt::KeepAspectRatio));
        const QImage image = reader.read();
        if (image.isNull()) {
            fail(QStringLiteral("The picture could not be read: %1").arg(reader.errorString()));
            return false;
        }
        const bool alpha = image.hasAlphaChannel();
        target = storage_dir_ + QStringLiteral("/background-") + stamp + (alpha ? QStringLiteral(".png") : QStringLiteral(".jpg"));
        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly) || !image.save(&out, alpha ? "PNG" : "JPG", alpha ? -1 : 92) || !out.commit()) {
            fail(QStringLiteral("The picture could not be saved to the settings folder."));
            return false;
        }
    }

    const QString previous = background_file_;
    background_file_ = QDir::cleanPath(target);
    background_name_ = info.fileName();
    background_animated_ = animated;
    background_mode_ = kModePicture;
    store_->setValue("ui/background/file", background_file_);
    store_->setValue("ui/background/name", background_name_);
    store_->setValue("ui/background/animated", background_animated_);
    store_->setValue("ui/background/mode", background_mode_);
    if (!previous.isEmpty() && previous != background_file_ && ownsFile(previous)) QFile::remove(previous);
    Q_EMIT backgroundChanged();
    return true;
}

void AppSettings::clearBackground() {
    const QString previous = background_file_;
    const bool was_picture = background_mode_ == QLatin1String(kModePicture);
    if (previous.isEmpty() && !was_picture) return;
    forgetBackgroundFile();
    if (was_picture) {
        background_mode_ = kModeWaves;
        store_->setValue("ui/background/mode", background_mode_);
    }
    if (!previous.isEmpty() && ownsFile(previous)) QFile::remove(previous);
    Q_EMIT backgroundChanged();
}

void AppSettings::resetAppearance() {
    clearError();
    setTheme(QString::fromLatin1(tds_theme::kDefaultTheme));
    setAccent(QString::fromLatin1(tds_theme::kThemeAccent));
    clearBackground();
    setBackgroundMode(QString::fromLatin1(kModeWaves));
    setBackgroundDim(kDefaultDim);
}

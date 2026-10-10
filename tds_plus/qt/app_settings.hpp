// language: C++17, file: app_settings.hpp, runtime: Qt 6.10, target: tds+ desktop UI
// User preferences of the Qt UI: theme, accent, background and window behaviour. Stored with
// QSettings (the registry under tds-plus\tds+ on Windows); every value read back is validated, so a
// hand-edited or stale entry falls back to its default instead of breaking the window.
//
// A background picture is imported, not referenced: it is checked, shrunk when it is large and
// copied to the app's data folder, so the original can be moved or deleted.
#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class AppSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY paletteChanged)
    Q_PROPERTY(QVariantList themes READ themes CONSTANT)
    Q_PROPERTY(QVariantList accents READ accents NOTIFY paletteChanged)
    Q_PROPERTY(QString backgroundMode READ backgroundMode WRITE setBackgroundMode NOTIFY backgroundChanged)
    Q_PROPERTY(QUrl backgroundUrl READ backgroundUrl NOTIFY backgroundChanged)
    Q_PROPERTY(QString backgroundName READ backgroundName NOTIFY backgroundChanged)
    Q_PROPERTY(bool backgroundAnimated READ backgroundAnimated NOTIFY backgroundChanged)
    Q_PROPERTY(bool hasBackgroundFile READ hasBackgroundFile NOTIFY backgroundChanged)
    Q_PROPERTY(int backgroundDim READ backgroundDim WRITE setBackgroundDim NOTIFY backgroundDimChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY windowOptionsChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY windowOptionsChanged)
    Q_PROPERTY(bool startInTray READ startInTray WRITE setStartInTray NOTIFY windowOptionsChanged)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY windowOptionsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    static constexpr qint64 kMaxFileBytes = 40 * 1024 * 1024;
    static constexpr int kMaxStillSide = 2560;     // stills are shrunk to this on the long side
    static constexpr int kMaxAnimatedSide = 4096;  // animations are copied as they are, up to this
    static constexpr int kDefaultDim = 55;
    static constexpr int kMaxDim = 90;

    // `store` is not owned. `storage_dir` receives the imported background pictures.
    AppSettings(QSettings* store, const QString& storage_dir, QObject* parent = nullptr);

    QString theme() const { return theme_; }
    QString accent() const { return accent_; }
    QVariantMap palette() const;
    QVariantList themes() const;
    QVariantList accents() const;
    QString backgroundMode() const { return background_mode_; }
    QUrl backgroundUrl() const;
    QString backgroundPath() const { return background_file_; }
    QString backgroundName() const { return background_name_; }
    bool backgroundAnimated() const { return background_animated_; }
    bool hasBackgroundFile() const { return !background_file_.isEmpty(); }
    int backgroundDim() const { return background_dim_; }
    bool minimizeToTray() const { return minimize_to_tray_; }
    bool closeToTray() const { return close_to_tray_; }
    bool startInTray() const { return start_in_tray_; }
    bool alwaysOnTop() const { return always_on_top_; }
    QString lastError() const { return last_error_; }

    void setTheme(const QString& id);
    void setAccent(const QString& id);
    void setBackgroundMode(const QString& mode);
    void setBackgroundDim(int percent);
    void setMinimizeToTray(bool on);
    void setCloseToTray(bool on);
    void setStartInTray(bool on);
    void setAlwaysOnTop(bool on);

    // true: the picture is imported and shown. false: lastError says why; nothing changed.
    Q_INVOKABLE bool importBackground(const QUrl& file);
    Q_INVOKABLE void clearBackground();
    Q_INVOKABLE void resetAppearance();
    Q_INVOKABLE void clearError();

Q_SIGNALS:
    void themeChanged();
    void accentChanged();
    void paletteChanged();
    void backgroundChanged();
    void backgroundDimChanged();
    void windowOptionsChanged();
    void lastErrorChanged();

private:
    void load();
    void fail(const QString& message);
    void setWindowOption(bool& field, const char* key, bool on);
    void forgetBackgroundFile();
    bool ownsFile(const QString& path) const;

    QSettings* store_;
    QString storage_dir_;
    QString theme_;
    QString accent_;
    QString background_mode_;
    QString background_file_;
    QString background_name_;
    bool background_animated_ = false;
    int background_dim_ = kDefaultDim;
    bool minimize_to_tray_ = false;
    bool close_to_tray_ = false;
    bool start_in_tray_ = false;
    bool always_on_top_ = false;
    QString last_error_;
};

// language: C++17, file: control_model.hpp, runtime: Qt 6.10, target: Windows 11 desktop
#pragma once

#include "../backend_api.hpp"
#include <QAbstractListModel>
#include <QStringList>
#include <QTimer>

class ControlModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(int slots READ slots NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool offline READ offline CONSTANT)
    Q_PROPERTY(QString query READ query WRITE set_query NOTIFY queryChanged)
    Q_PROPERTY(bool liveOnly READ live_only WRITE set_live_only NOTIFY liveOnlyChanged)
    Q_PROPERTY(QStringList logLines READ log_lines NOTIFY stateChanged)
    Q_PROPERTY(QString saveStatus READ save_status NOTIFY saveStatusChanged)
    Q_PROPERTY(bool saveError READ save_error NOTIFY saveStatusChanged)

public:
    enum Role { KeyRole = Qt::UserRole + 1, NameRole, TowerRole, CooldownRole,
                ModeRole, SecondsRole, LiveRole };
    explicit ControlModel(bool offline, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool running() const { return snapshot_.running; }
    bool ready() const { return !snapshot_.abilities.empty(); }
    QString phase() const;
    int slots() const { return snapshot_.slots; }
    int total() const { return static_cast<int>(snapshot_.abilities.size()); }
    int count() const { return static_cast<int>(rows_.size()); }
    bool offline() const { return offline_; }
    QString query() const { return query_; }
    bool live_only() const { return live_only_; }
    QStringList log_lines() const;
    QString save_status() const { return save_status_; }
    bool save_error() const { return save_error_; }
    void set_query(const QString& value);
    void set_live_only(bool value);
    Q_INVOKABLE void toggle_running();
    Q_INVOKABLE void set_mode(const QString& key, int mode);
    Q_INVOKABLE void set_seconds(const QString& key, int seconds);
    void refresh();

Q_SIGNALS:
    void stateChanged();
    void countChanged();
    void queryChanged();
    void liveOnlyChanged();
    void saveStatusChanged();
    void exitRequested();

private:
    void rebuild_rows();
    void save_rule(const QString& key, int mode, int seconds);
    TdsSnapshot snapshot_;
    std::vector<TdsAbilityState> rows_;
    QString query_;
    QString save_status_;
    bool live_only_ = false;
    bool offline_ = false;
    bool save_error_ = false;
    QTimer poll_timer_;
    QTimer notice_timer_;
};

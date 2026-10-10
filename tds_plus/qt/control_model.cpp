// language: C++17, file: control_model.cpp, runtime: Qt 6.10, target: Windows 11 desktop
#include "control_model.hpp"
#include <algorithm>

ControlModel::ControlModel(bool offline, QObject* parent)
    : QAbstractListModel(parent), offline_(offline) {
    connect(&poll_timer_, &QTimer::timeout, this, &ControlModel::refresh);
    poll_timer_.start(150);
    notice_timer_.setSingleShot(true);
    connect(&notice_timer_, &QTimer::timeout, this, [this] {
        save_status_.clear();
        Q_EMIT saveStatusChanged();
    });
    refresh();
}

int ControlModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant ControlModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const auto& entry = rows_[index.row()];
    switch (role) {
    case KeyRole: return QString::fromStdString(entry.key);
    case NameRole: return QString::fromStdString(entry.name);
    case TowerRole: return QString::fromStdString(entry.tower);
    case CooldownRole: return entry.cooldown;
    case ModeRole: return entry.mode;
    case SecondsRole: return entry.seconds;
    case LiveRole: return entry.live;
    default: return {};
    }
}

QHash<int, QByteArray> ControlModel::roleNames() const {
    return {{KeyRole, "abilityKey"}, {NameRole, "abilityName"}, {TowerRole, "towerName"},
            {CooldownRole, "cooldown"}, {ModeRole, "abilityMode"},
            {SecondsRole, "chainSeconds"}, {LiveRole, "isLive"}};
}

QString ControlModel::phase() const {
    const auto& phase = snapshot_.phase;
    if (phase == "waiting for Roblox") return QStringLiteral("Waiting for Roblox");
    if (phase == "unknown roblox version") return QStringLiteral("Unknown Roblox version");
    if (phase == "access denied") return QStringLiteral("Access denied");
    if (phase == "in match") return QStringLiteral("In match");
    if (phase == "match over") return QStringLiteral("Match ended");
    if (phase == "lobby / intermission") return QStringLiteral("Lobby");
    if (phase == "offline preview") return QStringLiteral("Preview");
    return QStringLiteral("Connecting…");
}

QStringList ControlModel::log_lines() const {
    QStringList lines;
    for (const auto& line : snapshot_.log) lines.push_back(QString::fromStdString(line).trimmed());
    return lines;
}

void ControlModel::refresh() {
    auto next = tds_snapshot();
    const bool state_changed = next.running != snapshot_.running || next.phase != snapshot_.phase
        || next.slots != snapshot_.slots || next.abilities.size() != snapshot_.abilities.size()
        || next.log != snapshot_.log;
    const bool should_exit = next.exit_requested && !snapshot_.exit_requested;
    snapshot_ = std::move(next);
    rebuild_rows();
    if (state_changed) Q_EMIT stateChanged();
    if (should_exit) Q_EMIT exitRequested();
}

void ControlModel::rebuild_rows() {
    std::vector<TdsAbilityState> next;
    const auto needle = query_.trimmed();
    for (const auto& entry : snapshot_.abilities) {
        if (live_only_ && !entry.live) continue;
        if (!needle.isEmpty()
            && !QString::fromStdString(entry.name).contains(needle, Qt::CaseInsensitive)
            && !QString::fromStdString(entry.tower).contains(needle, Qt::CaseInsensitive)) continue;
        next.push_back(entry);
    }
    const bool same_keys = next.size() == rows_.size()
        && std::equal(next.begin(), next.end(), rows_.begin(), [](const auto& a, const auto& b) {
            return a.key == b.key;
        });
    if (!same_keys) {
        beginResetModel();
        rows_ = std::move(next);
        endResetModel();
        Q_EMIT countChanged();
        return;
    }
    for (int row = 0; row < static_cast<int>(next.size()); ++row) {
        const auto& a = next[row];
        const auto& b = rows_[row];
        if (a.name == b.name && a.tower == b.tower && a.cooldown == b.cooldown
            && a.mode == b.mode && a.seconds == b.seconds && a.live == b.live) continue;
        rows_[row] = a;
        Q_EMIT dataChanged(index(row), index(row));
    }
}

void ControlModel::set_query(const QString& value) {
    if (query_ == value) return;
    query_ = value;
    rebuild_rows();
    Q_EMIT queryChanged();
}

void ControlModel::set_live_only(bool value) {
    if (live_only_ == value) return;
    live_only_ = value;
    rebuild_rows();
    Q_EMIT liveOnlyChanged();
}

void ControlModel::toggle_running() {
    if (!ready()) return;
    tds_set_running(!running());
    refresh();
}

void ControlModel::save_rule(const QString& key, int mode, int seconds) {
    save_error_ = !tds_set_rule(key.toStdString(), mode, seconds);
    save_status_ = save_error_ ? QStringLiteral("Could not save changes") : QStringLiteral("Saved");
    notice_timer_.start(save_error_ ? 6000 : 2400);
    refresh();
    Q_EMIT saveStatusChanged();
}

void ControlModel::set_mode(const QString& key, int mode) {
    for (const auto& entry : snapshot_.abilities) {
        if (entry.key != key.toStdString()) continue;
        save_rule(key, mode, entry.seconds);
        return;
    }
}

void ControlModel::set_seconds(const QString& key, int seconds) {
    for (const auto& entry : snapshot_.abilities) {
        if (entry.key != key.toStdString()) continue;
        save_rule(key, entry.mode, std::clamp(seconds, 1, 600));
        return;
    }
}

// language: C++17, file: smoke_test.hpp, runtime: Qt Test 6.10, target: isolated desktop UI verification
#pragma once
#include "control_model.hpp"
#include <QFile>
#include <QDir>
#include <QInputMethodEvent>
#include <QFontInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTextStream>
#include <QtTest/QTest>

inline QQuickItem* find_item(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = find_item(child, name)) return found;
    return nullptr;
}

inline int run_smoke_test(QQuickWindow* window, ControlModel& model, const QString& directory) {
    QFile report(directory + "/smoke_report.txt");
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) return 10;
    QTextStream output(&report);
    int failures = 0;
    auto check = [&](bool okay, const QString& message) {
        output << (okay ? "PASS " : "FAIL ") << message << '\n';
        output.flush();
        if (!okay) ++failures;
    };
    auto click = [&](const QString& name) {
        auto* item = find_item(window->contentItem(), name);
        if (!item || !item->isVisible()) { check(false, "visible control: " + name); return; }
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                         item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        QTest::qWait(80);
    };
    auto type = [&](const QString& text) {
        QInputMethodEvent event;
        event.setCommitString(text);
        QCoreApplication::sendEvent(window, &event);
        QTest::qWait(80);
    };
    auto rule = [&]() {
        for (const auto& entry : tds_snapshot().abilities)
            if (entry.key == "call to arms") return entry;
        return TdsAbilityState{};
    };
    auto scene_rect = [](QQuickItem* item) {
        return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    };
    auto fits_view = [&](const QString& name) {
        auto* item = find_item(window->contentItem(), name);
        if (!item || !item->isVisible()) return false;
        const auto bounds = scene_rect(item);
        if (!QRectF(0, 0, window->width(), window->height()).contains(bounds)) return false;
        for (auto* parent = item->parentItem(); parent; parent = parent->parentItem())
            if (parent->clip() && !scene_rect(parent).contains(bounds)) return false;
        return true;
    };

    model.refresh();
    const QFontInfo ui_font(QGuiApplication::font());
    check(ui_font.family() == QStringLiteral("Geist Mono") && ui_font.fixedPitch() && ui_font.weight() >= QFont::Medium,
          "bundled Geist Mono renders at Medium weight without system installation");
    check(model.total() >= 24, "wiki catalog loaded");
    check(!model.running(), "autocast starts paused");
    auto* ambient = find_item(window->contentItem(), "ambientBackground");
    check(ambient && ambient->property("shaderReady").toBool(), "background shader renders on the native scene graph");
    if (ambient && window->property("motionEnabled").toBool()) {
        QTest::mouseMove(window, QPoint(window->width() - 26, window->height() - 28));
        QTest::qWait(300);
        const QRect region(window->width() - 400, 8, 80, 12);
        const auto first = window->grabWindow().copy(region).convertToFormat(QImage::Format_RGB32);
        const double start = ambient->property("elapsed").toDouble();
        QTest::qWait(1400);
        const auto second = window->grabWindow().copy(region).convertToFormat(QImage::Format_RGB32);
        int changed = 0;
        for (int y = 0; y < first.height(); ++y)
            for (int x = 0; x < first.width(); ++x)
                if (first.pixel(x, y) != second.pixel(x, y)) ++changed;
        check(ambient->property("elapsed").toDouble() > start + 0.5 && changed > region.width() * region.height() / 50,
              "background pixels move without interaction");
        window->showMinimized();
        QTest::qWait(150);
        const double paused = ambient->property("elapsed").toDouble();
        QTest::qWait(200);
        check(ambient->property("elapsed").toDouble() == paused, "motion pauses while minimized");
        window->showNormal();
        window->requestActivate();
        QTest::qWait(350);
        check(ambient->property("elapsed").toDouble() > paused, "motion resumes after restore");
    }
    click("searchInput");
    type("Commander");
    check(model.count() == 2, "search matches tower and ability names");
    click("mode_chain_call to arms");
    check(rule().mode == 1, "chain click reaches the real backend");

    auto* interval = find_item(window->contentItem(), "interval_call to arms");
    check(interval && interval->isVisible(), "chain interval is editable");
    if (interval) {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                         interval->mapToScene(QPointF(28, 17)).toPoint());
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        type("17");
        QTest::keyClick(window, Qt::Key_Return);
        QTest::qWait(100);
        check(rule().seconds == 17, "typed interval commits to backend");
    }
    QFile rules(directory + "/tds_ui_rules.ini");
    check(rules.open(QIODevice::ReadOnly) && rules.readAll().contains("Call to Arms = chain 17"),
          "settings persist in existing INI format");
    rules.close();
    click("mode_off_call to arms");
    check(rule().mode == 2, "off click reaches backend");
    click("mode_chain_call to arms");
    click("runButton");
    check(tds_snapshot().running, "start button toggles engine state");
    click("runButton");
    check(!tds_snapshot().running, "stop button toggles engine state");
    click("liveFilterButton");
    check(model.count() == 0, "live filter has honest empty state offline");
    model.set_live_only(false);
    QTest::qWait(100);
    check(window->grabWindow().save(directory + "/preview.png"), "native window screenshot saved");
    click("searchInput");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    QTest::qWait(100);
    if (auto* list = find_item(window->contentItem(), "abilityList")) {
        const auto end = list->property("contentHeight").toDouble() - list->height();
        list->setProperty("contentY", end);
        QTest::qWait(80);
        check(list->property("contentY").toDouble() > 0, "catalog scroll reaches lower entries");
    }
    window->resize(380, 340);
    if (auto* list = find_item(window->contentItem(), "abilityList")) list->setProperty("contentY", 0);
    QTest::qWait(180);
    window->grabWindow();
    check(window->size() == QSize(380, 340), "native window shrinks to 380 by 340");
    click("searchInput");
    type("Commander");
    QTest::qWait(100);
    window->grabWindow();
    for (const auto* name : {"runButton", "searchInput", "liveFilterButton", "mode_auto_call to arms",
                             "mode_chain_call to arms", "mode_off_call to arms", "interval_call to arms"}) {
        if (fits_view(name)) continue;
        if (auto* item = find_item(window->contentItem(), name)) {
            const auto bounds = scene_rect(item);
            output << "CONTROL " << name << " x=" << bounds.x() << " y=" << bounds.y()
                   << " width=" << bounds.width() << " height=" << bounds.height() << '\n';
        }
    }
    check(fits_view("runButton") && fits_view("searchInput") && fits_view("liveFilterButton")
          && fits_view("mode_auto_call to arms") && fits_view("mode_chain_call to arms")
          && fits_view("mode_off_call to arms") && fits_view("interval_call to arms"),
          "all essential controls fit the smallest window");
    auto* label = find_item(window->contentItem(), "label_call to arms");
    auto* chain = find_item(window->contentItem(), "mode_chain_call to arms");
    interval = find_item(window->contentItem(), "interval_call to arms");
    check(label && chain && interval && !scene_rect(label).intersects(scene_rect(chain))
          && !scene_rect(interval).intersects(scene_rect(chain)), "narrow rows keep labels and controls separate");
    if (interval) {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                         interval->mapToScene(QPointF(28, 17)).toPoint());
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        type("23");
        QTest::keyClick(window, Qt::Key_Return);
        QTest::qWait(100);
        check(rule().seconds == 23, "chain interval remains editable in the smallest window");
    }
    click("logButton");
    QTest::qWait(250);
    auto* popup = window->findChild<QObject*>("activityPopup");
    check(popup && popup->property("visible").toBool()
          && QRectF(0, 0, window->width(), window->height()).contains(QRectF(
              popup->property("x").toDouble(), popup->property("y").toDouble(),
              popup->property("width").toDouble(), popup->property("height").toDouble())),
          "activity popup fits the smallest window");
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(160);
    check(popup && !popup->property("visible").toBool(), "Escape closes the activity popup");
    check(window->grabWindow().save(directory + "/preview_compact.png"), "minimum-size screenshot saved");
    click("searchInput");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    window->resize(860, 700);
    QTest::qWait(150);
    window->grabWindow();
    check(fits_view("mode_chain_call to arms"), "wide table restores after growing the window");
    window->grabWindow().save(directory + "/preview_wide.png");
    window->resize(560, 480);
    QTest::qWait(200);
    window->grabWindow().save(directory + "/preview_overview.png");
    if (qEnvironmentVariableIsSet("TDS_CAPTURE_MOTION")) {
        window->resize(560, 480);
        if (auto* list = find_item(window->contentItem(), "abilityList")) list->setProperty("contentY", 0);
        QTest::qWait(500);
        QDir().mkpath(directory + "/motion_frames");
        QTest::mouseMove(window, QPoint(280, 8));
        for (int frame = 0; frame < 72; ++frame) {
            if (frame == 22) click("mode_off_call to arms");
            if (frame == 35) click("mode_chain_call to arms");
            if (frame == 48) QTest::mouseMove(window, QPoint(260, 8));
            window->grabWindow().save(directory + QString("/motion_frames/frame_%1.png").arg(frame, 3, 10, QLatin1Char('0')));
            QTest::qWait(70);
        }
        window->grabWindow().save(directory + "/preview_overview.png");
    }
    output << "RESULT " << failures << " failures\n";
    return failures ? 1 : 0;
}

inline int run_attach_test(QQuickWindow* window, ControlModel& model, const QString& directory) {
    QFile report(directory + "/attach_report.txt");
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) return 10;
    QTextStream output(&report);
    for (int attempt = 0; attempt < 60; ++attempt) {
        model.refresh();
        if (model.phase() == QStringLiteral("Lobby")
            || model.phase() == QStringLiteral("In match")
            || model.phase() == QStringLiteral("Match ended")) break;
        QTest::qWait(250);
    }
    bool attached = false;
    for (const auto& line : model.log_lines())
        if (line.startsWith("tds+ attached:")) attached = true;
    output << (attached ? "PASS " : "FAIL ") << "existing engine attaches to Roblox\n";
    output << (!model.running() ? "PASS " : "FAIL ") << "engine stays paused\n";
    model.set_mode("call to arms", 1);
    bool reloaded = false;
    for (int attempt = 0; attempt < 40; ++attempt) {
        QTest::qWait(150);
        model.refresh();
        for (const auto& line : model.log_lines())
            if (line.startsWith("config reloaded:")) reloaded = true;
        if (reloaded) break;
    }
    output << (reloaded ? "PASS " : "FAIL ") << "QML settings reload in live engine\n";
    bool commander_detected = true;
    if (qEnvironmentVariableIsSet("TDS_EXPECT_LIVE_COMMANDER")) {
        commander_detected = false;
        for (const auto& ability : tds_snapshot().abilities)
            if (ability.key == "call to arms" && ability.live) commander_detected = true;
        commander_detected = commander_detected && model.phase() == QStringLiteral("In match") && model.slots() > 0;
        output << (commander_detected ? "PASS " : "FAIL ") << "sandbox Commander is live and match stays active\n";
    }
    output << "phase: " << model.phase() << "\nslots: " << model.slots() << '\n';
    window->grabWindow().save(directory + "/preview_live.png");
    return attached && reloaded && commander_detected && !model.running() ? 0 : 1;
}

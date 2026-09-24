#include "app/LaunchOptions.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QComboBox>
#include <QAbstractItemView>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include "ui/RangeTimeline.h"
#include <cstdlib>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir config;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config.path());
    auto check = [](bool condition) {
        if (!condition)
            std::abort();
    };
    auto parse = [](const QStringList &args, LaunchOptions &options) {
        QCommandLineParser parser;
        configureLaunchParser(parser);
        QString error;
        return parser.parse(QStringList{"mpv-clipper"} + args) &&
               readLaunchOptions(parser, options, error);
    };
    LaunchOptions options;
    check(parse({}, options) && options.timeSec == 0);
    const QString path = QString::fromUtf8("/tmp/caf\xc3\xa9 [clip's].mkv");
    check(parse({"--source", path, "--time", "754.233"}, options));
    check(options.source == path && options.timeSec == 754.233);
    check(parse({"--launch-payload", "/tmp/context file.json"}, options));
    check(options.payloadFile == "/tmp/context file.json");
    for (const auto &value : {"-1", "nan", "inf", "1e999", "abc", ""})
        check(!parse({"--source", path, "--time", value}, options));
    check(!parse({"--time", "2"}, options));
    check(!parse({"--source", path, "--launch-payload", "x.json"}, options));
    check(parse({"--launch-stdin", "--wait-for-close"}, options) && options.payloadStdin);
    check(!parse({"--launch-stdin", "--source", path}, options));
    check(!parse({"--launch-stdin", "--launch-payload", "x.json"}, options));
    check(!parse({"--source", ""}, options));
    check(!parse({"--source"}, options));
    check(!parse({"unexpected.mkv"}, options));
    check(!parse({"--unknown"}, options));
    RangeTimeline range;
    range.resize(1000, 88); range.setRange(10, 2, 8, 5);
    int boundary = 0; double value = 0; bool final = false;
    QObject::connect(&range, &RangeTimeline::edited, &app, [&](int b, double v, bool f) { boundary = b; value = v; final = f; });
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 19), QPointF(200, 19), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&range, &press);
    check(boundary == 1 && std::abs(value - 2) < 0.001);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(500, 40), QPointF(500, 40), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&range, &release);
    check(boundary == 1 && value == 5 && final && !range.dragging() && range.selectedBoundary() == 1);
    auto click = [&](double px) {
        QMouseEvent down(QEvent::MouseButtonPress, QPointF(px, 19), QPointF(px, 19), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(px, 19), QPointF(px, 19), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&range, &down); QApplication::sendEvent(&range, &up);
    };
    click(805);
    check(range.selectedBoundary() == 2 && boundary == 2 && value == 8);
    click(500);
    check(range.selectedBoundary() == 0 && boundary == 3 && value == 5);
    click(205);
    check(range.selectedBoundary() == 1 && value == 2);
    range.setClips({{2, 8}}, 0);
    check(range.selectedBoundary() == 0);

    // Zoom changes only the viewport, and keeps the time under the pointer fixed.
    range.setRange(7200, 3600, 3602, 3601);
    boundary = 0;
    QWheelEvent wheel(QPointF(500, 19), QPointF(500, 19), {}, QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&range, &wheel);
    check(range.visibleEnd() - range.visibleStart() < 7200 && boundary == 0);
    check(std::abs(range.visibleEnd() - range.visibleStart() - 7200 / 1.1) < 0.001);
    check(std::abs((range.visibleStart() + range.visibleEnd()) / 2 - 3600) < 0.001);
    QWheelEvent offCenter(QPointF(250, 19), QPointF(250, 19), {}, QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    double anchor = range.visibleStart() + (range.visibleEnd() - range.visibleStart()) * 0.25;
    QApplication::sendEvent(&range, &offCenter);
    check(std::abs(range.visibleStart() + (range.visibleEnd() - range.visibleStart()) * 0.25 - anchor) < 0.001);
    QWheelEvent zoomOut(QPointF(250, 19), QPointF(250, 19), {}, QPoint(0, -120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    double oldSpan = range.visibleEnd() - range.visibleStart();
    QApplication::sendEvent(&range, &zoomOut);
    check(range.visibleEnd() - range.visibleStart() > oldSpan && boundary == 0);
    range.zoomToClip();
    check(range.visibleStart() < 3600 && range.visibleEnd() > 3602);
    check(range.visibleEnd() - range.visibleStart() < 3);
    double left = range.visibleStart(), span = range.visibleEnd() - left;
    double handleX = (3600 - left) / span * range.width();
    QMouseEvent zoomPress(QEvent::MouseButtonPress, QPointF(handleX, 19), QPointF(handleX, 19), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&range, &zoomPress);
    check(boundary == 1 && std::abs(value - 3600) < 0.001);
    QApplication::sendEvent(&range, &release);
    check(final && std::abs(value - (left + span / 2)) < 0.001);
    boundary = 0;
    QMouseEvent overviewPress(QEvent::MouseButtonPress, QPointF(750, 68), QPointF(750, 68), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&range, &overviewPress);
    QMouseEvent overviewRelease(QEvent::MouseButtonRelease, QPointF(850, 68), QPointF(850, 68), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&range, &overviewRelease);
    check(boundary == 0 && range.visibleStart() > 6000);
    // Offscreen markers must not become editable phantom handles at either edge.
    QMouseEvent edgePress(QEvent::MouseButtonPress, QPointF(1, 19), QPointF(1, 19), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&range, &edgePress); check(boundary == 3);
    QApplication::sendEvent(&range, &release);
    range.fitVideo(); check(range.visibleStart() == 0 && range.visibleEnd() == 7200);
    for (int i = 0; i < 50; ++i) range.zoom(2);
    check(std::abs(range.visibleEnd() - range.visibleStart() - 0.1) < 0.000001);
    range.setRange(0, 0, 0, 0); QApplication::sendEvent(&range, &wheel);
    check(range.visibleStart() == 0 && range.visibleEnd() == 0);
    range.setRange(0.05, 0, 0.05, 0); range.zoom(2);
    check(range.visibleEnd() == 0.05);

    MainWindow window;
    window.show();
    QTimer::singleShot(0, &app, [&] {
        check(window.isVisible());
        check(!window.isFullScreen());
        check(window.size() == QSize(1100, 720));
        check(window.findChild<QProgressBar *>()->isHidden());
        window.resize(1600, 900); QApplication::processEvents();
        check(window.findChild<QPushButton *>("exportButton")->width() < 180);
        check(window.findChild<QPushButton *>("cancelButton")->width() < 140);
        check(window.findChild<QLabel *>("statusDot")->size() == QSize(8, 8));
        window.resize(1100, 720); QApplication::processEvents();
        for (auto *box : window.findChildren<QComboBox *>()) {
            if (box->accessibleName() != "Resolution") continue;
            box->showPopup(); QApplication::processEvents();
            check(box->view()->window()->geometry().top() >= box->mapToGlobal(QPoint(0, box->height())).y());
            box->hidePopup();
        }
        if (!qEnvironmentVariableIsEmpty("MPV_CLIPPER_SCREENSHOT"))
            check(window.grab().save(qEnvironmentVariable("MPV_CLIPPER_SCREENSHOT")));
        window.close();
        app.quit();
    });
    return app.exec();
}

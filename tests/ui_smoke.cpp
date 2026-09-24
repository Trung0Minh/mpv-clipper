#include "ui/MainWindow.h"
#include "ui/RangeTimeline.h"
#include "media/Timecode.h"
#include <QtWidgets>
#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <stdexcept>
#include <QSurfaceFormat>
#include <source_location>

static void until(const std::function<bool()> &condition, std::source_location location = std::source_location::current())
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 12000) QTest::qWait(30);
    if (!condition()) throw std::runtime_error("UI operation timed out at line " + std::to_string(location.line()));
}
int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    // GitHub's Windows service session has no usable desktop/OpenGL surface.
    if (qEnvironmentVariableIsSet("GITHUB_ACTIONS")) return 77;
#endif
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) return 77;
#endif
#ifdef Q_OS_MACOS
    QSurfaceFormat surface;
    surface.setVersion(3, 2); surface.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(surface);
#endif
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dir.path());
    try {
        QString source = dir.filePath("sample.mkv");
        QProcess ffmpeg; ffmpeg.start("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i",
            "testsrc2=size=320x180:rate=24:duration=3", "-c:v", "libx264", "-preset", "ultrafast", source});
        if (!ffmpeg.waitForFinished(10000) || ffmpeg.exitCode()) throw std::runtime_error("Fixture failed");
        MainWindow window; window.show();
        LaunchPayload payload; payload.source = source; payload.time = 0.5; window.open(payload);
        auto button = [&](const QString &text) {
            for (auto *b : window.findChildren<QPushButton *>()) if (b->text() == text) return b;
            throw std::runtime_error(("Missing button " + text).toStdString());
        };
        auto *play = button("Play");
        until([&] { return play->isEnabled(); });
        QLineEdit *start = nullptr, *end = nullptr;
        for (auto *field : window.findChildren<QLineEdit *>()) {
            if (field->accessibleName() == "Start timestamp") start = field;
            if (field->accessibleName() == "End timestamp") end = field;
        }
        if (!start || !end || parseTimecode(start->text()).value() != 0 || parseTimecode(end->text()).value() < 2.9)
            throw std::runtime_error("Initial full-video selection failed");
        start->setText("0.500"); QMetaObject::invokeMethod(start, "editingFinished");
        auto *timeline = window.findChild<RangeTimeline *>();
        auto selectMarker = [&](double time) {
            QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier,
                QPoint(qRound(time / 3.0 * timeline->width()), 19));
        };
        selectMarker(0.5);
        QTest::mouseClick(button("<"), Qt::LeftButton);
        until([&] { return parseTimecode(start->text()).value() < 0.5; });
        until([&] { return button(">")->isEnabled(); });
        QTest::mouseClick(button(">"), Qt::LeftButton);
        until([&] { return std::abs(parseTimecode(start->text()).value() - 0.5) < 0.005; });
        end->setText("2.000"); QMetaObject::invokeMethod(end, "editingFinished");
        selectMarker(2.0);
        QTest::mouseClick(button(">"), Qt::LeftButton);
        until([&] { return parseTimecode(end->text()).value() > 2.0; });
        until([&] { return button("<")->isEnabled(); });
        QTest::mouseClick(button("<"), Qt::LeftButton);
        until([&] { return std::abs(parseTimecode(end->text()).value() - 2.0) < 0.005; });
        selectMarker(1.0);
        if (timeline->selectedBoundary() != 0) throw std::runtime_error("Seek did not clear marker selection");
        auto *current = window.findChild<QLabel *>("currentTime");
        until([&] { return button(">")->isEnabled(); });
        QTest::qWait(200); // Allow the exact seek to report its decoded frame timestamp.
        const double before = parseTimecode(current->text()).value();
        QTest::mouseClick(button(">"), Qt::LeftButton);
        until([&] { return parseTimecode(current->text()).value() > before + 0.02; });
        until([&] { return button("<")->isEnabled(); });
        QTest::mouseClick(button("<"), Qt::LeftButton);
        until([&] { return std::abs(parseTimecode(current->text()).value() - before) < 0.005; });
        if (std::abs(parseTimecode(start->text()).value() - 0.5) > 0.005 || std::abs(parseTimecode(end->text()).value() - 2.0) > 0.005)
            throw std::runtime_error("Playback stepping changed clip boundaries");
        start->setText("1.000"); QMetaObject::invokeMethod(start, "editingFinished");
        end->setText("1.500"); QMetaObject::invokeMethod(end, "editingFinished");
        for (auto *box : window.findChildren<QCheckBox *>()) if (box->text() == "Loop") box->setChecked(false);
        QTest::mouseClick(play, Qt::LeftButton);
        until([&] { return play->text() == "Pause"; });
        until([&] { return play->text() == "Play"; });
        if (!window.findChild<QLabel *>("currentTime")->text().startsWith("00:00:01.500"))
            throw std::runtime_error("Playback did not stop at selected End");
        auto *format = window.findChildren<QComboBox *>().first();
        for (auto *box : window.findChildren<QComboBox *>()) if (box->accessibleName() == "Format") format = box;
        format->setCurrentText("GIF");
        for (auto *box : window.findChildren<QComboBox *>())
            if (box->accessibleName() == "Subtitles" && box->findText("Softsub") >= 0) throw std::runtime_error("GIF softsub present");
        format->setCurrentText("MP4");
        auto *exportButton = button("Export Clip");
        until([&] { return exportButton->isEnabled(); });
        QTest::mouseClick(exportButton, Qt::LeftButton);
        until([&] { return button("Copy Path")->isVisible(); });
        QTest::mouseClick(button("Copy Path"), Qt::LeftButton);
        if (!QFileInfo::exists(QApplication::clipboard()->text())) throw std::runtime_error("Export result missing");
        QComboBox *mode = nullptr;
        for (auto *box : window.findChildren<QComboBox *>()) if (box->accessibleName() == "Clipping mode") mode = box;
        if (!mode) throw std::runtime_error("Missing Multi Clip mode");
        mode->setCurrentText("Multi Clip");
        auto *list = window.findChild<QListWidget *>();
        auto *name = window.findChild<QLineEdit *>("filename");
        QTest::mouseClick(button("Add Clip"), Qt::LeftButton);
        format->setCurrentText("GIF");
        end->setText("1.750"); QMetaObject::invokeMethod(end, "editingFinished");
        list->setCurrentRow(0);
        if (format->currentText() != "MP4" || parseTimecode(end->text()).value() != 1.5)
            throw std::runtime_error("First clip settings were changed by second clip");
        list->setCurrentRow(1);
        if (format->currentText() != "GIF" || parseTimecode(end->text()).value() != 1.75)
            throw std::runtime_error("Second clip settings were not restored");
        QString gifPath = QDir(window.findChild<QLineEdit *>("directory")->text()).filePath(name->text());
        list->setCurrentRow(0);
        // Give the first clip a fresh path rather than overwriting the earlier single export.
        name->setFocus(); name->selectAll(); QTest::keyClicks(name, "batch-first.mp4");
        QMetaObject::invokeMethod(name, "editingFinished");
        QString mp4Path = QDir(window.findChild<QLineEdit *>("directory")->text()).filePath(name->text());
        QTest::mouseClick(button("Export All"), Qt::LeftButton);
        until([&] { return list->count() == 2 && list->item(0)->text().endsWith("Done") && list->item(1)->text().endsWith("Done"); });
        if (!QFileInfo::exists(mp4Path) || !QFileInfo::exists(gifPath)) throw std::runtime_error("Independent batch outputs missing");
        until([&] { return button("Export All")->isEnabled(); });
        list->setCurrentRow(1);
        QTest::mouseClick(button("Delete Clip"), Qt::LeftButton);
        if (list->count() != 1 || format->currentText() != "MP4") throw std::runtime_error("Delete clip failed");
        QString screenshot = qEnvironmentVariable("MPV_CLIPPER_SCREENSHOT");
        if (!screenshot.isEmpty()) window.grab().save(screenshot);
        window.close();
        qInfo() << "Rendered preview, frame step, range playback and UI export passed";
    } catch (const std::exception &e) { qCritical() << e.what(); return 1; }
    return 0;
}

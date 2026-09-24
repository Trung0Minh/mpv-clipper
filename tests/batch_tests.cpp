#include <QtWidgets>
#include <QtTest>
#include "ui/MainWindow.h"
#include "media/MpvPlayer.h"
#include <stdexcept>

static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void until(const std::function<bool()> &done) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < 20000) QTest::qWait(10);
    require(done(), "Timed out");
}
class BatchTest {
public:
    static void run() {
        QTemporaryDir dir;
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dir.path());
        QString source = dir.filePath("source.mkv");
        QProcess fixture;
        fixture.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=24:duration=3", "-c:v", "libx264", source});
        require(fixture.waitForFinished(10000) && fixture.exitCode() == 0, "Fixture failed");
        MainWindow w;
        until([&] { return w.caps.available; });
        // Exercise real export/UI state without requiring an OpenGL display.
        w.player = new MpvPlayer(&w);
        w.media.source = source; w.media.duration = 3; w.media.fps = 24;
        w.media.width = 160; w.media.height = 90; w.media.video = 0;
        w.playbackReady = true; w.start = 0.25; w.end = 1.0;
        w.directory->setText(dir.path()); w.updateRange(); w.updateFilename();
        w.quality->setCurrentIndex(1); w.loop->setChecked(false);
        w.mode->setCurrentIndex(1);
        w.addClip->click();
        w.quality->setCurrentIndex(2); w.loop->setChecked(true);
        w.format->setCurrentText("GIF"); w.end = 1.5; w.updateRange();
        w.directory->setText(dir.filePath("second")); QDir().mkpath(w.directory->text());
        w.saveClip();
        w.selectClip(0);
        require(w.format->currentText() == "MP4" && w.end == 1.0 && w.directory->text() == dir.path() && w.quality->currentIndex() == 1 && !w.loop->isChecked(), "Clip 1 changed");
        w.selectClip(1);
        require(w.format->currentText() == "GIF" && w.end == 1.5 && w.directory->text().endsWith("second") && w.quality->currentIndex() == 2 && w.loop->isChecked(), "Clip 2 lost settings");
        w.beginBatch(); require(!w.progress->isHidden(), "Progress missing during export");
        until([&] { return !w.batching; });
        require(w.progress->isHidden(), "Progress visible after export");
        for (const auto &clip : w.clips) require(clip.state == "Done" && QFileInfo::exists(clip.output.destination), "Batch output missing");
        require(w.clips[0].output.destination != w.clips[1].output.destination, "Output collision");
        // Remove only this test's outputs before exercising fresh exports.
        for (const auto &clip : w.clips) QFile::remove(clip.output.destination);
        w.beginBatch(); w.cancelBatch = true; w.job.cancel();
        until([&] { return !w.batching; });
        require(w.clips[0].state == "Cancelled" && w.clips[1].state == "Cancelled", "Cancel all did not cancel queue");
        w.beginBatch(); w.job.cancel(); until([&] { return !w.batching; });
        require(w.clips[0].state == "Cancelled" && w.clips[1].state == "Done", "Cancel current stopped subsequent clip");
        QFile::remove(w.clips[1].output.destination);
        // Force a source failure after validation, then restore it and retry.
        w.beginBatch(); QFile::rename(source, source + ".held");
        until([&] { return !w.batching; });
        QFile::rename(source + ".held", source);
        require(w.clips[1].state == "Failed", "Expected failed queued clip");
        w.beginBatch(true); until([&] { return !w.batching; });
        require(w.clips[1].state == "Done", "Retry failed");
        w.selectClip(1); w.deleteClip->click();
        require(w.clips.size() == 1 && w.format->currentText() == "MP4", "Delete failed");
        if (qEnvironmentVariableIsSet("MPV_CLIPPER_BENCHMARK")) {
            const auto saved = w.clips;
            while (w.clips.size() < 100) w.clips.append(saved.first());
            w.refreshClipList();
            QSignalSpy resets(w.clipList->model(), &QAbstractItemModel::modelReset);
            QElapsedTimer bench; bench.start();
            for (int i = 0; i < 200; ++i) w.setBoundary(1, 0.1 + (i % 50) * 0.001, false);
            w.setBoundary(1, 0.25, true);
            qInfo() << "200 drag updates / 100 clips:" << bench.elapsed() << "ms; list resets:" << resets.count();
            require(resets.count() == 0, "Dragging rebuilt the clip list");
            require(w.clips[w.activeClip].output.start == 0.25, "Final drag position was not saved");
            w.clips = saved; w.refreshClipList();
        }
        if (!qEnvironmentVariableIsEmpty("MPV_CLIPPER_SCREENSHOT")) {
            w.show(); QApplication::processEvents();
            w.grab().save(qEnvironmentVariable("MPV_CLIPPER_SCREENSHOT"));
        }
        qInfo() << "Independent settings, mixed-format batch, cancel current/all, retry and delete passed";
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try { BatchTest::run(); } catch (const std::exception &e) { qCritical() << e.what(); return 1; }
    return 0;
}

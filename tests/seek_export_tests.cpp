#include "export/ExportJob.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QtEndian>
#include <cmath>
#include <stdexcept>

static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static QByteArray run(const QStringList &args, const QString &cwd = {})
{
    QProcess process; if (!cwd.isEmpty()) process.setWorkingDirectory(cwd);
    process.start("ffmpeg", args);
    check(process.waitForFinished(120000), "FFmpeg timed out");
    if (process.exitCode()) throw std::runtime_error(process.readAllStandardError().toStdString());
    return process.readAllStandardOutput();
}
static MediaInfo probe(const QString &source)
{
    QProcess process; process.start("ffprobe", {"-v", "error", "-show_streams", "-show_format", "-of", "json", source});
    check(process.waitForFinished(10000) && !process.exitCode(), "Probe failed");
    return MediaInfo::parse(process.readAllStandardOutput(), source);
}
static QByteArray video(const QString &path) {
    return run({"-v", "error", "-i", path, "-map", "0:v:0", "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"});
}
static QByteArray timing(const QString &path)
{
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-show_packets", "-show_entries", "packet=stream_index,pts_time,duration_time", "-of", "compact", path});
    check(process.waitForFinished(10000) && !process.exitCode(), "Timing probe failed");
    return process.readAllStandardOutput();
}
static void compareAudio(const QByteArray &actual, const QByteArray &reference)
{
    check(!actual.isEmpty() && actual.size() == reference.size(), "Audio sample count changed");
    double best = 1e100;
    // Seeking can change timestamp resampling by one sample and AAC noise synthesis.
    // Require matching packet times separately and at most a one-sample phase offset.
    for (int shift = -1; shift <= 1; ++shift) {
        double error = 0, energy = 0;
        for (qsizetype i = 1; i < actual.size() / 2 - 1; ++i) {
            double a = qFromLittleEndian<qint16>(actual.constData() + i * 2);
            double b = qFromLittleEndian<qint16>(reference.constData() + (i + shift) * 2);
            error += (a - b) * (a - b); energy += b * b;
        }
        best = std::min(best, std::sqrt(error / std::max(energy, 1.0)));
    }
    check(best < 0.01, "Audio synchronization/content changed beyond one-sample tolerance");
}
static QStringList legacy(QStringList args)
{
    args.removeAll("-copyts"); args.removeAll("-start_at_zero"); args.removeAll("-noaccurate_seek");
    int seek = args.indexOf("-ss");
    if (seek >= 0) { args.removeAt(seek); args.removeAt(seek); }
    return args;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir; check(dir.isValid(), "Temporary directory failed");
        QFile ass(dir.filePath("external.ass")); check(ass.open(QIODevice::WriteOnly), "Subtitle fixture failed");
        ass.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,DejaVu Sans,32,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n"
            "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:48.00,0:00:55.00,Default,,0,0,0,,{\\move(60,260,450,260,0,7000)}Overlap animation\n"
            "Dialogue: 0,0:00:54.00,0:00:56.00,Default,,0,0,0,,Second cue\n"); ass.close();
        QString source = dir.filePath("long.mkv");
        run({"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=24:duration=60",
             "-f", "lavfi", "-i", "sine=frequency=437:duration=60", "-i", ass.fileName(),
             "-map", "0:v", "-map", "1:a", "-map", "2:s", "-c:v", "libx265", "-pix_fmt", "yuv420p10le",
             "-preset", "ultrafast", "-x265-params", "keyint=240:min-keyint=240:scenecut=0:pools=2:frame-threads=2",
             "-c:a", "aac", "-c:s", "ass", source});
        auto media = probe(source);
        Track external; external.external = ass.fileName(); external.codec = "ass";
        media.subtitles.append(external);
        check(QFile::copy(source, dir.filePath("internal")), "Staging fixture failed");
        QByteArray silentPixels;
        for (int scenario = 0; scenario < 7; ++scenario) {
            ExportSettings settings; settings.start = 53.537; settings.end = 54.375; settings.audio = 0;
            QString subtitle;
            if (scenario == 1 || scenario == 2) {
                settings.subtitleMode = "Hardsub"; settings.subtitle = scenario - 1;
                subtitle = scenario == 1 ? "internal" : "external.ass";
            }
            if (scenario == 3) {
                settings.subtitleMode = "Softsub"; settings.subtitle = 0;
                QFile clipped(dir.filePath("clipped.ass")); check(clipped.open(QIODevice::WriteOnly), "Clipped fixture failed");
                QFile original(ass.fileName()); check(original.open(QIODevice::ReadOnly), "Read subtitle fixture failed");
                clipped.write(trimAss(original.readAll(), settings.start, settings.end)); clipped.close(); subtitle = "clipped.ass";
            }
            if (scenario == 4) { settings.format = "gif"; settings.gifWidth = 320; settings.subtitleMode = "Hardsub"; settings.subtitle = 1; subtitle = "external.ass"; }
            if (scenario == 5) settings.format = "webm";
            if (scenario == 6) settings.format = "mkv";
            QString fast = dir.filePath(QString("fast-%1.%2").arg(scenario).arg(settings.format));
            QString slow = dir.filePath(QString("slow-%1.%2").arg(scenario).arg(settings.format));
            auto args = exportArguments(media, settings, fast, subtitle);
            check(args.indexOf("-ss") < args.indexOf("-i") && args.contains("-copyts"), "Missing fast input seek");
            QElapsedTimer timer; timer.start(); run(args, dir.path()); auto fastMs = timer.elapsed();
            args.last() = slow; timer.restart(); run(legacy(args), dir.path()); auto slowMs = timer.elapsed();
            const auto frames = video(fast);
            check(!frames.isEmpty() && frames == video(slow), "Fast seek changed decoded video frames");
            check(timing(fast) == timing(slow), "Fast seek changed packet timestamps/durations");
            if (scenario == 0) silentPixels = frames;
            if (scenario == 1 || scenario == 2) check(frames != silentPixels, "Subtitle missing after seek");
            if (settings.format != "gif") {
                const auto audioArgs = QStringList{"-v", "error", "-i", fast, "-map", "0:a:0", "-f", "s16le", "-"};
                auto referenceArgs = audioArgs; referenceArgs[3] = slow;
                compareAudio(run(audioArgs), run(referenceArgs));
            }
            if (scenario == 3) {
                auto text = run({"-v", "error", "-i", fast, "-map", "0:s:0", "-f", "srt", "-"});
                check(text.contains("Overlap animation") && text.contains("Second cue") && text.contains("00:00:00,"), "Subtitle overlap/rebasing failed");
            }
            qInfo() << "Scenario" << scenario << "fast" << fastMs << "ms; full decode" << slowMs << "ms; matching frames and timing";
        }
        ExportSettings soft; soft.start = 53.537; soft.end = 54.375;
        soft.audio = 0; soft.subtitle = 0; soft.subtitleMode = "Softsub";
        soft.destination = dir.filePath("prepared-softsub.mp4");
        ExportJob job; QEventLoop wait; bool success = false; QString failure;
        QObject::connect(&job, &ExportJob::finished, &wait, [&](bool ok, QString message, QString details) {
            success = ok; failure = message + "\n" + details; wait.quit();
        });
        QTimer timeout; timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &job, &ExportJob::cancel);
        timeout.start(30000); job.start(media, soft, false); if (job.running()) wait.exec();
        if (!success) throw std::runtime_error(failure.toStdString());
        auto subtitleText = run({"-v", "error", "-i", soft.destination, "-map", "0:s:0", "-f", "srt", "-"});
        check(subtitleText.contains("Overlap animation") && subtitleText.contains("Second cue"), "Prepared internal softsub cues missing");
        // Nonzero container start times must retain source-relative trimming.
        QString shifted = dir.filePath("shifted.mkv");
        run({"-v", "error", "-i", source, "-map", "0", "-c", "copy", "-output_ts_offset", "5", shifted});
        auto shiftedMedia = probe(shifted);
        ExportSettings settings; settings.start = 53.537; settings.end = 54.375; settings.audio = 0;
        QString fast = dir.filePath("offset-fast.mp4"), slow = dir.filePath("offset-slow.mp4");
        auto args = exportArguments(shiftedMedia, settings, fast); run(args);
        args.last() = slow; run(legacy(args));
        check(!video(fast).isEmpty() && video(fast) == video(slow) && timing(fast) == timing(slow), "Nonzero source start changed timing");
        QString variable = dir.filePath("variable.mkv");
        run({"-v", "error", "-i", source, "-map", "0:v", "-vf", "select='if(lt(t,54),not(mod(n,2)),1)'",
             "-fps_mode", "vfr", "-c:v", "libx264", "-preset", "ultrafast", variable});
        auto variableMedia = probe(variable); settings.audio = -1;
        fast = dir.filePath("vfr-fast.mp4"); slow = dir.filePath("vfr-slow.mp4");
        args = exportArguments(variableMedia, settings, fast); run(args);
        args.last() = slow; run(legacy(args));
        check(!video(fast).isEmpty() && video(fast) == video(slow) && timing(fast) == timing(slow), "Variable frame cadence changed");
        qInfo() << "Fast seek matches full decode for HEVC 10-bit, long GOPs, non-keyframe cuts, all formats, audio, animated hardsubs, overlapping softsubs and nonzero timestamps";
    } catch (const std::exception &error) { qCritical().noquote() << error.what(); return 1; }
    return 0;
}

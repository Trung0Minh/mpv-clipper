#include "app/LaunchPayload.h"
#include "media/Timecode.h"
#include "export/ExportJob.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <stdexcept>

static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static QByteArray run(const QString &program, const QStringList &args)
{
    QProcess p; p.start(program, args);
    check(p.waitForFinished(60000), "Process timeout");
    if (p.exitCode() != 0) throw std::runtime_error(p.readAllStandardError().toStdString());
    return p.readAllStandardOutput();
}
static QJsonObject probe(const QString &path)
{
    return QJsonDocument::fromJson(run("ffprobe", {"-v", "error", "-show_streams", "-show_format", "-of", "json", path})).object();
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        check(timecode(754.233) == "00:12:34.233", "Time formatting");
        check(parseTimecode("12:34.233").value() == 754.233, "Time parsing");
        for (auto invalid : {"nan", "1:60", "-2", "1:2:3:4", "1.1234", ""}) check(!parseTimecode(invalid), "Invalid time accepted");
        check(safeFilename("../bad:name/").contains(':') == false, "Filename sanitization");
        bool rejected = false;
        try { LaunchPayload::parse("{\"schemaVersion\":2}"); } catch (...) { rejected = true; }
        check(rejected, "Invalid payload accepted");
        auto trimmed = trimAss("[Events]\nDialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,Overlap\nDialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,Outside\n", 1, 3);
        check(trimmed.contains("00:00:00.00,00:00:01.00") && !trimmed.contains("Outside"), "Subtitle clipping");
        QTemporaryDir dir; check(dir.isValid(), "Temporary folder");
        QString source = dir.filePath(QString::fromUtf8("caf\xc3\xa9 [movie's].mkv"));
        QString subs = dir.filePath(QString::fromUtf8("ti\xe1\xba\xbfng [sub's].srt"));
        QFile file(subs); check(file.open(QIODevice::WriteOnly), "Subtitle fixture");
        file.write("1\n00:00:00,000 --> 00:00:01,500\nOverlap at start\n\n2\n00:00:01,750 --> 00:00:03,500\nMiddle and end\n"); file.close();
        run("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24:duration=4",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=4", "-i", subs, "-map", "0:v", "-map", "1:a", "-map", "2:s",
            "-c:v", "libx264", "-preset", "ultrafast", "-c:a", "aac", "-c:s", "ass", source});
        auto metadata = probe(source);
        auto media = MediaInfo::parse(QJsonDocument(metadata).toJson(), source);
        check(media.width == 320 && media.audio.size() == 1 && media.subtitles.size() == 1, "Probe metadata");
        Track external; external.external = subs; external.codec = "subrip"; external.type = "subtitle"; external.ordinal = 1;
        media.subtitles.append(external);
        Capabilities caps = Capabilities::detect(); check(caps.available, "FFmpeg capabilities");
        int passed = 0;
        for (auto format : {"mp4", "mkv", "webm", "gif"}) {
            check(caps.formatSupported(format), "Required format unavailable in test environment");
            for (auto mode : {"None", "Hardsub", "Softsub"}) {
                if (QString(format) == "gif" && QString(mode) == "Softsub") continue;
                for (int subtitle : {0, 1}) {
                    ExportSettings s; s.format = format; s.subtitleMode = mode; s.start = 1; s.end = 2.5;
                    s.audio = 0; s.subtitle = subtitle; s.gifWidth = 160; s.quality = 2;
                    if (QString(mode) == "None" && subtitle == 1) s.audio = -1;
                    s.destination = dir.filePath(QString("%1-%2-%3.%1").arg(format, mode).arg(subtitle));
                    auto error = exportValidation(media, s, caps);
                    if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
                    ExportJob job; QEventLoop wait; bool success = false; QString detail;
                    QObject::connect(&job, &ExportJob::finished, &wait, [&](bool ok, QString message, QString log) { success = ok; detail = message + "\n" + log; wait.quit(); });
                    QTimer deadline; deadline.setSingleShot(true); QObject::connect(&deadline, &QTimer::timeout, &wait, [&] { job.cancel(); wait.quit(); });
                    deadline.start(60000); job.start(media, s, false); if (job.running()) wait.exec();
                    if (!success) throw std::runtime_error(detail.toStdString());
                    auto output = probe(s.destination);
                    double duration = output["format"].toObject()["duration"].toString().toDouble();
                    check(duration >= 1.45 && duration <= 1.65, "Output duration mismatch");
                    int audio = 0, subtitles = 0;
                    for (auto stream : output["streams"].toArray()) {
                        auto type = stream.toObject()["codec_type"].toString();
                        audio += type == "audio"; subtitles += type == "subtitle";
                        if (type == "video") check(stream.toObject()["width"].toInt() == (s.format == "gif" ? 160 : 320), "Output dimensions");
                    }
                    check(audio == (s.format == "gif" || s.audio < 0 ? 0 : 1), "Audio mapping");
                    check(subtitles == (s.subtitleMode == "Softsub" ? 1 : 0), "Subtitle mapping");
                    run("ffmpeg", {"-v", "error", "-i", s.destination, "-map", "0:v:0", "-frames:v", "1", "-f", "null", "-"});
                    if (s.subtitleMode == "Hardsub") {
                        auto pixels = run("ffmpeg", {"-v", "error", "-i", s.destination, "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"});
                        auto baseline = run("ffmpeg", {"-v", "error", "-i", dir.filePath(QString("%1-None-0.%1").arg(format)), "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"});
                        check(pixels != baseline, "Hardsub did not change the rendered frame");
                    }
                    if (s.subtitleMode == "Softsub") {
                        auto text = run("ffmpeg", {"-v", "error", "-i", s.destination, "-map", "0:s:0", "-f", "srt", "-"});
                        check(text.contains("Overlap at start") && text.contains("00:00:00,"), "Overlapping subtitle lost");
                        check(text.contains("Middle and end"), "Middle subtitle lost");
                    }
                    qInfo().noquote() << "PASS" << format << mode << (subtitle ? "external" : "internal"); ++passed;
                }
            }
        }
        for (auto range : {std::pair{0.0, 0.125}, {0.537, 0.9}, {3.7, 3.95}}) {
            ExportSettings shortClip; shortClip.start = range.first; shortClip.end = range.second;
            shortClip.height = 1080;
            QString out = dir.filePath(QString("short-%1.mp4").arg(range.first));
            run("ffmpeg", exportArguments(media, shortClip, out));
            auto info = probe(out);
            double duration = info["format"].toObject()["duration"].toString().toDouble();
            check(std::abs(duration - (range.second - range.first)) < 0.06, "Short clip timing");
            check(info["streams"].toArray()[0].toObject()["height"].toInt() == 180, "Preset upscaled source");
            auto first = run("ffmpeg", {"-v", "error", "-i", out, "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"});
            auto reference = run("ffmpeg", {"-v", "error", "-i", source, "-ss", QString::number(range.first), "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"});
            check(first.size() == reference.size() && !first.isEmpty(), "First frame size mismatch");
            double squaredError = 0;
            for (qsizetype i = 0; i < first.size(); ++i) {
                int delta = static_cast<unsigned char>(first[i]) - static_cast<unsigned char>(reference[i]);
                squaredError += delta * delta;
            }
            check(squaredError / first.size() < 200, "First exported frame does not match accurate source seek");
        }
        ExportSettings custom; custom.start = 0; custom.end = 1; custom.customWidth = 161; custom.customHeight = 91; custom.fps = 30;
        QString customPath = dir.filePath("custom.mp4"); run("ffmpeg", exportArguments(media, custom, customPath));
        auto customVideo = probe(customPath)["streams"].toArray()[0].toObject();
        check(customVideo["width"].toInt() == 160 && customVideo["height"].toInt() == 90 && customVideo["avg_frame_rate"].toString() == "30/1", "Custom dimensions/FPS");
        ExportSettings s; s.start = 0; s.end = 4; s.destination = dir.filePath("existing.mp4");
        QFile existing(s.destination); check(existing.open(QIODevice::WriteOnly), "Existing fixture"); existing.write("keep me"); existing.close();
        ExportJob cancel; QEventLoop wait;
        QObject::connect(&cancel, &ExportJob::finished, &wait, [&](bool ok, QString, QString) { check(!ok, "Canceled export succeeded"); wait.quit(); });
        cancel.start(media, s, true); QTimer::singleShot(1, &cancel, &ExportJob::cancel); wait.exec();
        check(existing.open(QIODevice::ReadOnly) && existing.readAll() == "keep me", "Cancel destroyed existing target"); existing.close();
        check(QDir(dir.path()).entryList({".clipper-*"}, QDir::Files | QDir::Hidden).isEmpty(), "Partial output leaked");
        s.destination = dir.filePath(QString::fromUtf8("replace caf\xc3\xa9.mp4"));
        QFile replaceTarget(s.destination); check(replaceTarget.open(QIODevice::WriteOnly), "Replace fixture");
        replaceTarget.write("old contents"); replaceTarget.close();
        ExportJob replacement; bool replaced = false;
        QObject::connect(&replacement, &ExportJob::finished, &wait, [&](bool ok, QString, QString) { replaced = ok; wait.quit(); });
        replacement.start(media, s, true); wait.exec();
        check(replaced && probe(s.destination)["streams"].isArray(), "Unicode target replacement failed");
        s.destination = source; check(!exportValidation(media, s, caps).isEmpty(), "Source overwrite accepted");
        s.end = -1; bool invalidCommand = false;
        try { exportArguments(media, s, "out.mp4"); } catch (...) { invalidCommand = true; }
        check(invalidCommand, "Command builder accepted a reversed range");
        qInfo() << passed << "real export combinations passed; cancellation preserves the existing file.";
    } catch (const std::exception &e) { qCritical().noquote() << e.what(); return 1; }
    return 0;
}

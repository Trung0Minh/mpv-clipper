#include "ExportJob.h"
#include <QFileInfo>
#include <QDateTime>
#include <QTimer>
#include <cstdio>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

ExportJob::ExportJob(QObject *parent) : QObject(parent)
{
    connect(&process, &QProcess::readyReadStandardError, this, [this] {
        diagnostics = (diagnostics + process.readAllStandardError()).right(65536);
    });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        progressBuffer += process.readAllStandardOutput();
        int newline;
        while ((newline = progressBuffer.indexOf('\n')) >= 0) {
            auto line = progressBuffer.left(newline); progressBuffer.remove(0, newline + 1);
            if (line.startsWith("out_time_us=")) {
                bool ok; double us = line.mid(12).toDouble(&ok);
                if (ok) emit progress(std::clamp(int(us / 1e6 / (settings.end - settings.start) * 100), 0, 99));
            }
        }
        if (progressBuffer.size() > 4096) progressBuffer.clear();
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) complete(false, "Could not start FFmpeg. Check its installation.");
    });
    connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!busy) return;
        if (canceled) { complete(false, "Export canceled."); return; }
        if (code || status != QProcess::NormalExit) { complete(false, "Export failed. Your editing session is preserved."); return; }
        if (preparing) {
            QFile file(staging->filePath("original.ass"));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) {
                complete(false, "Could not read the subtitle preparation result."); return;
            }
            try {
                auto clipped = trimAss(file.readAll(), settings.start, settings.end);
                QFile dest(staging->filePath("clipped.ass"));
                if (!dest.open(QIODevice::WriteOnly) || dest.write(clipped) != clipped.size() || !dest.flush())
                    throw std::runtime_error("Could not save prepared subtitles.");
                dest.close(); subtitleInput = "clipped.ass"; preparing = false; encode();
            } catch (const std::exception &e) { complete(false, e.what()); }
            return;
        }
        QFileInfo target(settings.destination);
        if (replace) {
            if (target.isSymLink() || !target.isFile() || target.size() != oldSize ||
                target.lastModified().toMSecsSinceEpoch() != oldMtime) {
                complete(false, "The destination changed during export. Choose another filename."); return;
            }
#ifdef Q_OS_WIN
            const bool renamed = MoveFileExW(reinterpret_cast<LPCWSTR>(output->fileName().utf16()),
                reinterpret_cast<LPCWSTR>(settings.destination.utf16()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
            const bool renamed = std::rename(QFile::encodeName(output->fileName()).constData(), QFile::encodeName(settings.destination).constData()) == 0;
#endif
            if (!renamed) {
                complete(false, "Could not replace the destination file."); return;
            }
        } else if (!QFile::rename(output->fileName(), settings.destination)) {
            complete(false, "Could not save the export. The destination may already exist."); return;
        }
        emit progress(100); complete(true, "Export complete.");
    });
}
ExportJob::~ExportJob()
{
    disconnect(&process, nullptr, this, nullptr);
    process.kill(); process.waitForFinished(3000);
}
void ExportJob::complete(bool success, const QString &message)
{
    if (!busy) return;
    busy = false; output.reset(); staging.reset();
    emit finished(success, message, QString::fromUtf8(diagnostics));
}
void ExportJob::start(const MediaInfo &m, const ExportSettings &s, bool overwrite)
{
    if (busy) return;
    media = m; settings = s; replace = overwrite;
    busy = true; canceled = false; preparing = false;
    diagnostics.clear(); progressBuffer.clear(); subtitleInput.clear();
    QFileInfo target(s.destination);
    oldSize = target.size(); oldMtime = target.lastModified().toMSecsSinceEpoch();
    staging = std::make_unique<QTemporaryDir>();
    output = std::make_unique<QTemporaryFile>(target.absolutePath() + "/.clipper-XXXXXX." + s.format);
    if (!staging->isValid() || !output->open()) { complete(false, "Could not create temporary export files."); return; }
    output->close();
    process.setWorkingDirectory(staging->path());
    if (s.subtitleMode != "None") {
        const auto &track = m.subtitles[s.subtitle];
        QString source = track.external.isEmpty() ? m.source : track.external;
        if (s.subtitleMode == "Softsub") {
            preparing = true;
            process.start("ffmpeg", {"-hide_banner", "-nostdin", "-y", "-i", source,
                "-map", QString("0:%1").arg(track.external.isEmpty() ? track.index : 0),
                "-c:s", "ass", staging->filePath("original.ass")});
            return;
        }
        subtitleInput = source;
    }
    encode();
}
void ExportJob::encode()
{
    try { process.start("ffmpeg", exportArguments(media, settings, output->fileName(), subtitleInput)); }
    catch (const std::exception &e) { complete(false, e.what()); }
}
void ExportJob::cancel()
{
    if (!busy) return;
    canceled = true; process.terminate();
    QTimer::singleShot(1500, this, [this] { if (busy && canceled) process.kill(); });
}

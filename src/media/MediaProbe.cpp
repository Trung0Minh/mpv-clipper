#include "MediaProbe.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QTimer>
#include <cmath>
#include <stdexcept>

QString Track::label() const
{
    return QString("%1%2 - %3%4").arg(external.isEmpty() ? "Track " : "External ")
        .arg(external.isEmpty() ? QString::number(ordinal + 1) : QFileInfo(external).fileName())
        .arg(language.isEmpty() ? codec : language + " / " + codec)
        .arg(title.isEmpty() ? "" : " - " + title);
}

MediaInfo MediaInfo::parse(const QByteArray &data, const QString &source)
{
    auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) throw std::runtime_error("Media inspection returned invalid metadata.");
    MediaInfo m;
    m.source = source;
    auto root = doc.object();
    m.duration = root["format"].toObject()["duration"].toString().toDouble();
    for (const auto &entry : root["streams"].toArray()) {
        auto s = entry.toObject();
        QString type = s["codec_type"].toString();
        if (type == "video" && m.video < 0 && !s["disposition"].toObject()["attached_pic"].toInt()) {
            m.video = s["index"].toInt(-1);
            m.width = s["width"].toInt(); m.height = s["height"].toInt();
            for (const auto &data : s["side_data_list"].toArray()) {
                int rotation = qRound(data.toObject()["rotation"].toDouble());
                if (std::abs(rotation) % 180 == 90) std::swap(m.width, m.height);
            }
            auto fps = s["avg_frame_rate"].toString().split('/');
            if (fps.size() == 2 && fps[1].toDouble() > 0) m.fps = fps[0].toDouble() / fps[1].toDouble();
            if (!std::isfinite(m.fps) || m.fps < 0) m.fps = 0;
            if (m.duration <= 0) m.duration = s["duration"].toString().toDouble();
        } else if (type == "audio" || type == "subtitle") {
            Track t;
            t.index = s["index"].toInt(-1); t.type = type; t.codec = s["codec_name"].toString();
            auto tags = s["tags"].toObject();
            t.language = tags["language"].toString(); t.title = tags["title"].toString();
            t.preferred = s["disposition"].toObject()["default"].toInt() ||
                          s["disposition"].toObject()["forced"].toInt();
            auto &tracks = type == "audio" ? m.audio : m.subtitles;
            t.ordinal = tracks.size(); tracks.append(t);
        }
    }
    if (m.video < 0 || m.width <= 0 || m.height <= 0)
        throw std::runtime_error("This file has no usable video stream.");
    if (!std::isfinite(m.duration) || m.duration <= 0 || m.duration > 1e9)
        throw std::runtime_error("The video duration is unknown or invalid.");
    return m;
}

MediaProbe::MediaProbe(QObject *parent) : QObject(parent)
{
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &process, &QProcess::kill);
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        output += process.readAllStandardOutput();
        if (output.size() > 16 * 1024 * 1024) { process.kill(); errors = "Media metadata exceeds 16 MiB."; }
    });
    connect(&process, &QProcess::readyReadStandardError, this, [this] {
        errors = (errors + process.readAllStandardError()).right(16384);
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) emit failed("Could not start ffprobe. Check its installation.");
    });
    connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        timeout.stop();
        if (code || status != QProcess::NormalExit) { emit failed("Could not inspect video.\n" + errors); return; }
        try { emit ready(MediaInfo::parse(output, source)); }
        catch (const std::exception &e) { emit failed(e.what()); }
    });
}
MediaProbe::~MediaProbe() { process.kill(); process.waitForFinished(1000); }
void MediaProbe::start(const QString &path)
{
    if (process.state() != QProcess::NotRunning) return;
    source = path; output.clear(); errors.clear();
    process.start("ffprobe", {"-v", "error", "-show_format", "-show_streams", "-of", "json", path});
    timeout.start(30000);
}

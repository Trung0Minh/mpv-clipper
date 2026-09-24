#include "LaunchPayload.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDir>
#include <cmath>
#include <stdexcept>

QJsonObject LaunchPayload::json() const
{
    return {{"schemaVersion", 1}, {"source", source}, {"mediaTitle", title},
            {"timePos", time}, {"fromMpv", fromMpv},
            {"activeAudio", audio}, {"activeSubtitle", subtitle}};
}

LaunchPayload LaunchPayload::parse(const QByteArray &bytes)
{
    auto doc = QJsonDocument::fromJson(bytes);
    auto o = doc.object();
    if (bytes.size() > 1024 * 1024 || !doc.isObject() ||
        o.value("schemaVersion").toDouble() != 1 || !o.value("source").isString() ||
        !o.value("timePos").isDouble())
        throw std::runtime_error("Invalid or unsupported launch payload.");
    LaunchPayload p;
    p.source = o.value("source").toString();
    p.title = o.value("mediaTitle").toString();
    p.time = o.value("timePos").toDouble();
    p.fromMpv = o.value("fromMpv").toBool(o.contains("originalPaused"));
    p.audio = o.value("activeAudio").toObject();
    p.subtitle = o.value("activeSubtitle").toObject();
    if (!std::isfinite(p.time) || p.time < 0 || p.source.contains(QChar::Null))
        throw std::runtime_error("Invalid source or playback position.");
    if (!p.source.isEmpty()) {
        QFileInfo f(p.source);
        if (!f.isFile() || !f.isReadable())
            throw std::runtime_error("The source is not a readable local file.");
        p.source = f.absoluteFilePath();
    }
    return p;
}

LaunchPayload LaunchPayload::read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        throw std::runtime_error("Could not read launch payload (maximum 1 MiB).");
    auto p = parse(file.readAll());
    file.close();
    // Only consume files matching the bridge's private temporary-file convention.
    QFileInfo info(path);
    if (!info.isSymLink() && info.fileName().startsWith("mpv-clipper-") &&
        info.suffix() == "json" && info.absolutePath() == QDir::tempPath())
        file.remove();
    return p;
}

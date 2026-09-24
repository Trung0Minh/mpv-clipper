#pragma once
#include "media/MediaProbe.h"
#include <QSet>

struct ExportSettings {
    QString format = "mp4", subtitleMode = "None";
    int audio = -1, subtitle = -1;
    double start = 0, end = 0;
    int height = 0, customWidth = 0, customHeight = 0;
    int fps = 0, quality = 0, gifWidth = 640;
    QString destination;
};
struct Capabilities {
    QSet<QString> encoders, filters;
    bool available = false;
    QString error;
    bool formatSupported(const QString &format) const;
    static Capabilities detect();
};
bool textSubtitle(const QString &codec);
QString exportValidation(const MediaInfo &media, const ExportSettings &s, const Capabilities &caps);
QStringList exportArguments(const MediaInfo &media, const ExportSettings &s,
                            const QString &output, const QString &subtitleInput = {});
QByteArray trimAss(const QByteArray &ass, double start, double end);

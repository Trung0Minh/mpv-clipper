#pragma once
#include <QJsonObject>

struct LaunchPayload {
    QString source;
    QString title;
    double time = 0;
    bool fromMpv = false;
    QJsonObject audio;
    QJsonObject subtitle;
    QJsonObject json() const;
    static LaunchPayload parse(const QByteArray &bytes);
    static LaunchPayload read(const QString &path);
};

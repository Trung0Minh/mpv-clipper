#pragma once
#include <QProcess>
#include <QJsonObject>
#include <QTimer>

struct Track {
    int index = -1;
    int ordinal = -1;
    int mpvId = -1;
    QString type, codec, language, title, external;
    bool preferred = false;
    QString label() const;
};
struct MediaInfo {
    QString source;
    double duration = 0, fps = 0;
    int video = -1, width = 0, height = 0;
    QList<Track> audio, subtitles;
    static MediaInfo parse(const QByteArray &data, const QString &source);
};
class MediaProbe : public QObject {
    Q_OBJECT
public:
    explicit MediaProbe(QObject *parent = nullptr);
    ~MediaProbe();
    void start(const QString &source);
signals:
    void ready(MediaInfo info);
    void failed(QString message);
private:
    QProcess process;
    QTimer timeout;
    QByteArray output, errors;
    QString source;
};

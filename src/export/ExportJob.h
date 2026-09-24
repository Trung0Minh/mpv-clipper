#pragma once
#include "ExportCommandBuilder.h"
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <memory>

class ExportJob : public QObject {
    Q_OBJECT
public:
    explicit ExportJob(QObject *parent = nullptr);
    ~ExportJob();
    void start(const MediaInfo &media, const ExportSettings &settings, bool replace);
    void cancel();
    bool running() const { return busy; }
signals:
    void progress(int percent);
    void finished(bool success, QString message, QString details);
private:
    void encode();
    void complete(bool success, const QString &message);
    QProcess process;
    MediaInfo media;
    ExportSettings settings;
    bool busy = false, canceled = false, preparing = false, replace = false;
    QByteArray diagnostics, progressBuffer;
    std::unique_ptr<QTemporaryDir> staging;
    std::unique_ptr<QTemporaryFile> output;
    QString subtitleInput;
    qint64 oldSize = -1, oldMtime = -1;
};

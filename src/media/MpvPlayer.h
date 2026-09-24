#pragma once
#include <QObject>
#include <QTimer>
#include <QJsonArray>
#include <mpv/client.h>

class MpvPlayer : public QObject {
    Q_OBJECT
public:
    explicit MpvPlayer(QObject *parent = nullptr);
    ~MpvPlayer();
    mpv_handle *handle() const { return mpv; }
    void command(const QStringList &args, quint64 request = 0);
    void set(const char *name, const QString &value);
    void load(const QString &path);
    void seek(double seconds, bool exact = true);
    void pause(bool paused);
    void step(double from, bool backward);
    double position() const;
    QJsonArray tracks() const;
signals:
    void timeChanged(double seconds);
    void pauseChanged(bool paused);
    void loaded();
    void tracksChanged();
    void frameStepped(double seconds);
    void stepFinished();
    void endReached();
    void failed(QString message);
private:
    void events();
    mpv_handle *mpv = nullptr;
    QTimer poll, stepTimeout, scrubTimer;
    double scrubTarget = 0;
    int stepPhase = 0;
    bool stepBackward = false;
    double stepOrigin = 0;
    quint64 stepRequest = 0;
    bool stepSeekAcknowledged = false, stepSeekStarted = false;
};

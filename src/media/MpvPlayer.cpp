#include "MpvPlayer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <vector>
#include <stdexcept>
#include <clocale>

MpvPlayer::MpvPlayer(QObject *parent) : QObject(parent)
{
    // Qt adopts the desktop locale; libmpv requires C numeric formatting.
    std::setlocale(LC_NUMERIC, "C");
    mpv = mpv_create();
    if (!mpv) throw std::runtime_error("Could not create the video player.");
    for (auto [key, value] : {std::pair{"vo", "libmpv"}, {"terminal", "no"},
         {"osc", "no"}, {"input-default-bindings", "no"}, {"config", "no"},
         {"pause", "yes"}, {"keep-open", "yes"}, {"hwdec", "auto-safe"}})
        mpv_set_option_string(mpv, key, value);
    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv); mpv = nullptr;
        throw std::runtime_error("Could not initialize the video player.");
    }
    mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, 0, "track-list", MPV_FORMAT_NONE);
    mpv_observe_property(mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    connect(&poll, &QTimer::timeout, this, &MpvPlayer::events);
    poll.start(33);
    scrubTimer.setSingleShot(true); scrubTimer.setInterval(70);
    connect(&scrubTimer, &QTimer::timeout, this, [this] {
        command({"seek", QString::number(scrubTarget, 'f', 6), "absolute+keyframes"});
    });
    stepTimeout.setSingleShot(true);
    connect(&stepTimeout, &QTimer::timeout, this, [this] {
        stepPhase = 0; pause(true); emit stepFinished();
        emit failed("Frame stepping timed out. Try a nearby position.");
    });
}
MpvPlayer::~MpvPlayer() { poll.stop(); if (mpv) mpv_terminate_destroy(mpv); }
void MpvPlayer::command(const QStringList &args, quint64 request)
{
    QList<QByteArray> bytes;
    for (const auto &arg : args) bytes.append(arg.toUtf8());
    std::vector<const char *> argv;
    for (const auto &arg : bytes) argv.push_back(arg.constData());
    argv.push_back(nullptr);
    int error = mpv_command_async(mpv, request, argv.data());
    if (error < 0) emit failed(QString::fromUtf8(mpv_error_string(error)));
}
void MpvPlayer::set(const char *name, const QString &value)
{
    // Property writes must not wait for the decoder on the GUI thread.
    command({"set", QString::fromUtf8(name), value});
}
void MpvPlayer::load(const QString &path) { scrubTimer.stop(); pause(true); command({"loadfile", path, "replace"}); }
void MpvPlayer::pause(bool paused) { set("pause", paused ? "yes" : "no"); }
void MpvPlayer::seek(double seconds, bool exact)
{
    if (!exact) {
        scrubTarget = seconds;
        if (!scrubTimer.isActive()) scrubTimer.start();
        return;
    }
    // Release/click/frame-step always wins over a pending approximate preview.
    scrubTimer.stop();
    command({"seek", QString::number(seconds, 'f', 6), "absolute+exact"});
}
double MpvPlayer::position() const
{
    double t = 0; mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &t); return t;
}
QJsonArray MpvPlayer::tracks() const
{
    mpv_node node{};
    QJsonArray result;
    if (mpv_get_property(mpv, "track-list", MPV_FORMAT_NODE, &node) < 0) return result;
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; ++i) {
            auto &entry = node.u.list->values[i];
            QJsonObject o;
            if (entry.format != MPV_FORMAT_NODE_MAP) continue;
            for (int j = 0; j < entry.u.list->num; ++j) {
                auto &v = entry.u.list->values[j];
                QString key = entry.u.list->keys[j];
                if (v.format == MPV_FORMAT_STRING) o[key] = QString::fromUtf8(v.u.string);
                if (v.format == MPV_FORMAT_INT64) o[key] = double(v.u.int64);
                if (v.format == MPV_FORMAT_FLAG) o[key] = bool(v.u.flag);
            }
            result.append(o);
        }
    }
    mpv_free_node_contents(&node);
    return result;
}
void MpvPlayer::step(double from, bool backward)
{
    if (stepPhase) return;
    scrubTimer.stop();
    events();
    pause(true); stepBackward = backward; stepPhase = 1;
    stepSeekAcknowledged = stepSeekStarted = false;
    command({"seek", QString::number(from, 'f', 6), "absolute+exact"}, ++stepRequest);
    stepTimeout.start(8000);
}
void MpvPlayer::events()
{
    for (;;) {
        auto *event = mpv_wait_event(mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        if (stepPhase == 1 && event->event_id == MPV_EVENT_SEEK) stepSeekStarted = true;
        if (stepPhase == 1 && event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == stepRequest)
            stepSeekAcknowledged = event->error >= 0;
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto *p = static_cast<mpv_event_property *>(event->data);
            if (QString(p->name) == "track-list") emit tracksChanged();
            if (!p->data) continue;
            if (QString(p->name) == "time-pos") {
                double time = *static_cast<double *>(p->data);
                emit timeChanged(time);
                if (stepPhase == 2 && std::abs(time - stepOrigin) > 0.000001) {
                    stepPhase = 0; stepTimeout.stop(); pause(true);
                    emit frameStepped(time); emit stepFinished();
                }
            }
            if (QString(p->name) == "pause") emit pauseChanged(*static_cast<int *>(p->data));
            if (QString(p->name) == "eof-reached" && *static_cast<int *>(p->data)) emit endReached();
        } else if (event->event_id == MPV_EVENT_FILE_LOADED) emit loaded();
        else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART && stepPhase) {
            if (stepPhase == 1) {
                if (!stepSeekAcknowledged || !stepSeekStarted) continue;
                stepOrigin = position(); stepPhase = 2; command({stepBackward ? "frame-back-step" : "frame-step"});
            } else {
                stepPhase = 0; stepTimeout.stop(); pause(true);
                emit frameStepped(position()); emit stepFinished();
            }
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                emit failed(QString("Video playback failed: %1").arg(mpv_error_string(end->error)));
        } else if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->error < 0)
            emit failed(QString("Player command failed: %1").arg(mpv_error_string(event->error)));
    }
}

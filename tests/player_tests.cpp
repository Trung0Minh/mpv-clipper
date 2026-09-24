#include "media/MpvPlayer.h"
#include <mpv/render.h>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QJsonObject>
#include <memory>
#include <stdexcept>
#include <clocale>
#include <cstring>

static void until(const std::function<bool()> &predicate)
{
    QEventLoop loop; QTimer tick, timeout;
    tick.setInterval(20); timeout.setSingleShot(true);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    tick.start(); timeout.start(8000); loop.exec();
    if (!predicate()) throw std::runtime_error("Player operation timed out");
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir; QString source = dir.filePath("frames.mkv");
        QProcess ffmpeg; ffmpeg.start("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i",
            "testsrc2=size=160x90:rate=24:duration=3", "-f", "lavfi", "-i", "sine=duration=3",
            "-c:v", "libx264", "-preset", "ultrafast", "-c:a", "pcm_s16le", source});
        if (!ffmpeg.waitForFinished(10000) || ffmpeg.exitCode()) throw std::runtime_error("Fixture generation failed");
        bool localized = false;
        for (const char *locale : {"en_DK.utf8", "de_DE.UTF-8", "fr_FR.UTF-8"}) {
            if (std::setlocale(LC_NUMERIC, locale)) { localized = true; break; }
        }
        if (!localized) qWarning() << "No comma-decimal locale installed; locale regression coverage limited";
        MpvPlayer player;
        if (std::strcmp(std::setlocale(LC_NUMERIC, nullptr), "C") != 0)
            throw std::runtime_error("Player did not restore libmpv's required numeric locale");
        player.set("ao", "null");
        mpv_render_context *ctx = nullptr;
        mpv_render_param init[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)}, {MPV_RENDER_PARAM_INVALID, nullptr}};
        if (mpv_render_context_create(&ctx, player.handle(), init) < 0) throw std::runtime_error("Software render initialization failed");
        std::unique_ptr<mpv_render_context, decltype(&mpv_render_context_free)> render(ctx, mpv_render_context_free);
        alignas(64) unsigned char pixels[160 * 90 * 4]{};
        QTimer draw; draw.setInterval(16);
        QObject::connect(&draw, &QTimer::timeout, &app, [&] {
            if (!(mpv_render_context_update(ctx) & MPV_RENDER_UPDATE_FRAME)) return;
            int size[] = {160, 90}, block = 0; size_t stride = 160 * 4;
            mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, size},
                {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char *>("rgb0")}, {MPV_RENDER_PARAM_SW_STRIDE, &stride},
                {MPV_RENDER_PARAM_SW_POINTER, pixels}, {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block}, {MPV_RENDER_PARAM_INVALID, nullptr}};
            mpv_render_context_render(ctx, params);
        });
        draw.start(); bool loaded = false; double frame = -1;
        QObject::connect(&player, &MpvPlayer::loaded, &app, [&] { loaded = true; });
        QObject::connect(&player, &MpvPlayer::frameStepped, &app, [&](double t) { frame = t; });
        QObject::connect(&player, &MpvPlayer::failed, &app, [](QString error) { qWarning() << error; });
        qInfo() << "Loading";
        player.load(source); until([&] { return loaded; });
        bool audioMapped = false;
        for (const auto &entry : player.tracks()) {
            auto track = entry.toObject();
            if (track["type"].toString() == "audio" && track["ff-index"].toInt(-1) == 1) {
                audioMapped = true;
                player.set("aid", "no");
                player.set("aid", QString::number(track["id"].toInt()));
            }
        }
        if (!audioMapped) throw std::runtime_error("Preview/export audio identity did not match");
        qInfo() << "Seeking";
        player.seek(0.5); until([&] { return std::abs(player.position() - 0.5) < 0.005; });
        for (int i = 0; i < 100; ++i) player.seek(1.0 + i * 0.01, false);
        player.seek(0.5);
        QEventLoop settle; QTimer::singleShot(200, &settle, &QEventLoop::quit); settle.exec();
        if (std::abs(player.position() - 0.5) > 0.005) throw std::runtime_error("Pending scrub replaced exact release position");
        qInfo() << "Stepping forward";
        player.step(0.5, false); until([&] { return frame > 0.5; });
        if (std::abs(frame - (0.5 + 1.0 / 24)) > 0.005) throw std::runtime_error("Forward frame step mismatch");
        double forward = frame; frame = -1;
        qInfo() << "Stepping backward";
        player.step(forward, true); until([&] { return frame >= 0; });
        if (std::abs(frame - 0.5) > 0.005) throw std::runtime_error("Backward frame step mismatch");
        player.pause(false); until([&] { return player.position() > 0.8; }); player.pause(true);
        QString variable = dir.filePath("variable.mkv");
        ffmpeg.start("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=24:duration=3",
            "-vf", "select='if(lt(n,24),not(mod(n,2)),1)'", "-fps_mode", "vfr", "-c:v", "libx264", "-preset", "ultrafast", variable});
        if (!ffmpeg.waitForFinished(10000) || ffmpeg.exitCode()) throw std::runtime_error("VFR fixture failed");
        loaded = false; player.load(variable); until([&] { return loaded; });
        player.seek(0.5); until([&] { return std::abs(player.position() - 0.5) < 0.005; });
        frame = -1; player.step(0.5, false); until([&] { return frame > 0.5; });
        if (std::abs(frame - (0.5 + 1.0 / 12)) > 0.005) throw std::runtime_error("VFR frame step used nominal cadence");
        draw.stop();
        qInfo() << "Real decoded seeking, playback, bidirectional and VFR frame stepping passed";
    } catch (const std::exception &e) { qCritical() << e.what(); return 1; }
    return 0;
}

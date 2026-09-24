#include "app/SingleInstance.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <stdexcept>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().contains("--child")) {
            SingleInstance client;
            bool wait = app.arguments().contains("--wait");
            if (client.start("{\"test\":true}", wait)) return 2;
            return wait ? app.exec() : 0;
        }
        QTemporaryDir dir;
        qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());
        auto primary = std::make_unique<SingleInstance>();
        if (!primary->start("{}", false)) throw std::runtime_error("Primary instance failed");
        int received = 0;
        QObject::connect(primary.get(), &SingleInstance::received, &app, [&](QByteArray data) {
            if (data == "{\"test\":true}") ++received;
        });
        QProcess child;
        QEventLoop loop;
        QObject::connect(&child, &QProcess::finished, &loop, &QEventLoop::quit);
        QTimer deadline; deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        child.start(app.applicationFilePath(), {"--child"}); deadline.start(10000); loop.exec();
        if (child.state() != QProcess::NotRunning || child.exitCode() != 0 || received != 1)
            throw std::runtime_error("Payload forwarding failed");
        child.start(app.applicationFilePath(), {"--child", "--wait"});
        QTimer release; release.setSingleShot(true);
        QObject::connect(&release, &QTimer::timeout, &app, [&] {
            if (received != 2 || child.state() != QProcess::Running) app.exit(3);
            primary.reset();
        });
        release.start(1000); deadline.start(10000); loop.exec();
        if (child.state() != QProcess::NotRunning || child.exitCode() != 0 || received != 2)
            throw std::runtime_error("Wait-for-close lifecycle failed");
        qInfo() << "IPC forwarding and launcher lifetime passed";
    } catch (const std::exception &e) { qCritical() << e.what(); return 1; }
    return 0;
}

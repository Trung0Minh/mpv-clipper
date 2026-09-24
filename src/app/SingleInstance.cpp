#include "SingleInstance.h"
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QThread>
#include <QCryptographicHash>
#include <stdexcept>

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {}

bool SingleInstance::start(const QByteArray &payload, bool waitForClose)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) throw std::runtime_error("No private runtime directory is available.");
    QString name = dir + "/mpv-clipper-v1";
    lock = std::make_unique<QLockFile>(name + ".lock");
#ifdef Q_OS_WIN
    name = "mpv-clipper-" + QString::fromLatin1(QCryptographicHash::hash(dir.toUtf8(), QCryptographicHash::Sha256).toHex());
#endif
    if (lock->tryLock(0)) {
        QLocalServer::removeServer(name);
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(name)) throw std::runtime_error(("Could not create the application socket: " + server.errorString()).toStdString());
        connect(&server, &QLocalServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                auto *deadline = new QTimer(socket);
                deadline->setSingleShot(true);
                connect(deadline, &QTimer::timeout, socket, &QLocalSocket::abort);
                deadline->start(5000);
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer, deadline] {
                    buffer->append(socket->readAll());
                    if (buffer->size() > 1024 * 1024) { socket->abort(); return; }
                    if (!buffer->endsWith('\n')) return;
                    deadline->stop();
                    disconnect(socket, &QLocalSocket::readyRead, this, nullptr);
                    emit received(buffer->trimmed());
                    socket->write("OK\n");
                    socket->flush();
                    // Wait clients remain connected until the primary window closes.
                });
                if (socket->bytesAvailable()) QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection);
            }
        });
        return true;
    }
    for (int attempt = 0; attempt < 10; ++attempt) {
        client.connectToServer(name);
        if (client.waitForConnected(200)) break;
        client.abort();
        QThread::msleep(50);
    }
    if (client.state() != QLocalSocket::ConnectedState)
        throw std::runtime_error("Could not contact the existing Clipper window.");
    client.write(payload + '\n');
    if ((client.bytesToWrite() && !client.waitForBytesWritten(3000)) ||
        (!client.bytesAvailable() && !client.waitForReadyRead(3000)) ||
        !client.readAll().startsWith("OK"))
        throw std::runtime_error("The existing window did not acknowledge the launch.");
    if (waitForClose) {
        connect(&client, &QLocalSocket::disconnected, qApp, &QCoreApplication::quit);
        if (client.state() == QLocalSocket::UnconnectedState)
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    }
    return false;
}

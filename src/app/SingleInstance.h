#pragma once
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <memory>

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject *parent = nullptr);
    bool start(const QByteArray &payload, bool waitForClose);
signals:
    void received(QByteArray payload);
private:
    // Release the lock after the socket has been removed, not before.
    std::unique_ptr<QLockFile> lock;
    QLocalServer server;
    QLocalSocket client;
};

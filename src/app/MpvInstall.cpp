#include "MpvInstall.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDebug>
#include <stdexcept>

static QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(("Cannot read " + path).toStdString());
    return file.readAll();
}

static void write(const QString &path, const QJsonValue &encoded)
{
    if (encoded.isNull() || encoded.isUndefined()) {
        if (QFile::exists(path) && !QFile::remove(path)) throw std::runtime_error(("Cannot remove " + path).toStdString());
        return;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) throw std::runtime_error("Cannot create mpv configuration directory.");
    QSaveFile file(path);
    const auto data = QByteArray::fromBase64(encoded.toString().toLatin1());
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw std::runtime_error(("Cannot write " + path).toStdString());
}

static QJsonValue snapshot(const QString &path)
{
    return QFile::exists(path) ? QJsonValue(QString::fromLatin1(read(path).toBase64())) : QJsonValue(QJsonValue::Null);
}

void configureMpv(QString directory, bool uninstall)
{
    Q_INIT_RESOURCE(theme);
    if (directory.isEmpty()) {
#ifdef Q_OS_WIN
        const auto mpv = QStandardPaths::findExecutable("mpv.exe");
        const auto portable = QFileInfo(mpv).absolutePath() + "/portable_config";
        if (!mpv.isEmpty() && QDir(portable).exists()) directory = portable;
        else {
            const auto appData = qEnvironmentVariable("APPDATA");
            if (appData.isEmpty()) throw std::runtime_error("APPDATA is unavailable; pass --mpv-config.");
            directory = appData + "/mpv";
        }
#else
        directory = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + "/.config") + "/mpv";
#endif
    }
    directory = QDir(directory).absolutePath();
    const QString statePath = directory + "/script-opts/mpv-clipper-install.json";
    const QStringList files{"scripts/clipper.lua", "script-opts/clipper.conf"};
    const auto oldState = snapshot(statePath);
    QJsonObject state;
    if (!oldState.isNull()) {
        const auto doc = QJsonDocument::fromJson(read(statePath));
        state = doc.object();
        if (state["version"].toInt() != 1 || !state["before"].isObject() || !state["after"].isObject())
            throw std::runtime_error("Invalid Clipper installation record; preserve it and repair the installation manually.");
        for (const auto &name : files) {
            if (!state["before"].toObject().contains(name) || !state["after"].toObject()[name].isString())
                throw std::runtime_error("Incomplete Clipper installation record.");
            if (snapshot(directory + '/' + name) != state["after"].toObject()[name])
                throw std::runtime_error("Clipper's mpv files were edited after installation. Back up those edits and restore the recorded files before reinstalling or uninstalling.");
        }
    }
    if (uninstall && state.isEmpty()) { qInfo("No managed mpv bridge is installed here."); return; }
    QJsonObject previous;
    for (const auto &name : files) previous[name] = snapshot(directory + '/' + name);
    QJsonObject desired;
    if (uninstall) desired = state["before"].toObject();
    else {
        const auto executable = qEnvironmentVariable("MPV_CLIPPER_LAUNCHER",
            qEnvironmentVariable("APPIMAGE", QCoreApplication::applicationFilePath()));
        if (executable.contains('\n') || executable.contains('\r')) throw std::runtime_error("Unsupported executable path.");
        desired[files[0]] = QString::fromLatin1(read(":/clipper.lua").toBase64());
        QString config = previous[files[1]].isNull() ? QString{} : QString::fromUtf8(QByteArray::fromBase64(previous[files[1]].toString().toLatin1()));
        QStringList lines; bool hotkey = false;
        for (const auto &line : config.split('\n')) {
            const auto key = line.section('=', 0, 0).trimmed();
            if (key == "executable") continue;
            if (key == "hotkey") hotkey = true;
            lines.append(line);
        }
        lines.append("executable=" + QDir::fromNativeSeparators(executable));
        if (!hotkey) lines.append("hotkey=Ctrl+Shift+x");
        desired[files[1]] = QString::fromLatin1((lines.join('\n') + '\n').toUtf8().toBase64());
        if (state.isEmpty()) state = {{"version", 1}, {"before", previous}};
        state["after"] = desired;
    }
    try {
        for (const auto &name : files) write(directory + '/' + name, desired[name]);
        write(statePath, uninstall ? QJsonValue(QJsonValue::Null) : QJsonValue(QString::fromLatin1(QJsonDocument(state).toJson().toBase64())));
    } catch (...) {
        for (const auto &name : files) write(directory + '/' + name, previous[name]);
        write(statePath, oldState);
        throw;
    }
    qInfo().noquote() << (uninstall ? "Restored mpv configuration:" : "Installed mpv bridge (Ctrl+Shift+X):") << directory;
}

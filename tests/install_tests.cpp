#include "app/MpvInstall.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <stdexcept>

static QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
static void check(bool ok) { if (!ok) throw std::runtime_error("Installer regression failed"); }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        const auto path = temp.path() + "/mpv config";
        QDir().mkpath(path + "/script-opts");
        QFile original(path + "/script-opts/clipper.conf");
        check(original.open(QIODevice::WriteOnly));
        original.write("# keep this\nhotkey=Alt+c\nexecutable=old\n"); original.close();
        const auto saved = read(original.fileName());
        configureMpv(path, false);
        check(read(path + "/scripts/clipper.lua").contains("--launch-stdin"));
        check(read(original.fileName()).contains("hotkey=Alt+c"));
        configureMpv(path, false);
        QFile changed(path + "/scripts/clipper.lua"); check(changed.open(QIODevice::Append)); changed.write("-- user edit\n"); changed.close();
        bool refused = false;
        try { configureMpv(path, true); } catch (const std::exception &) { refused = true; }
        check(refused && read(changed.fileName()).endsWith("-- user edit\n"));
        check(changed.open(QIODevice::ReadWrite)); check(changed.resize(changed.size() - 13)); changed.close();
        configureMpv(path, true);
        check(read(original.fileName()) == saved && !QFile::exists(path + "/scripts/clipper.lua"));
        check(!QFile::exists(path + "/script-opts/mpv-clipper-install.json"));
        configureMpv(temp.path() + "/fresh", false);
        configureMpv(temp.path() + "/fresh", true);
        check(!QFile::exists(temp.path() + "/fresh/script-opts/clipper.conf"));
        if (argc > 1) {
            const auto config = temp.path() + "/CLI config";
            QProcess command;
            for (const auto &option : {"--install-mpv", "--uninstall-mpv"}) {
                command.start(QString::fromLocal8Bit(argv[1]), {option, "--mpv-config", config});
                check(command.waitForFinished(10000) && command.exitCode() == 0);
            }
            check(!QFile::exists(config + "/scripts/clipper.lua"));
        }
        return 0;
    } catch (const std::exception &e) { qCritical("%s", e.what()); return 1; }
}

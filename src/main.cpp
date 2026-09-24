#include "app/LaunchOptions.h"
#include "ui/MainWindow.h"
#include "util/Logging.h"
#include "app/LaunchPayload.h"
#include "app/SingleInstance.h"
#include "app/MpvInstall.h"
#include <QFile>
#include <QDir>
#include <memory>
#include <cstring>
#include <QSurfaceFormat>
#include <QJsonDocument>
#include <QMessageBox>
#include <QTimer>

#include <QApplication>

int main(int argc, char *argv[])
{
    bool setup = false;
    bool cliOnly = false;
    for (int i = 1; i < argc; ++i) {
        setup |= !std::strcmp(argv[i], "--install-mpv") || !std::strcmp(argv[i], "--uninstall-mpv");
        cliOnly |= !std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h") ||
            !std::strcmp(argv[i], "--help-all") || !std::strcmp(argv[i], "--version") ||
            !std::strcmp(argv[i], "-v");
    }
#ifdef Q_OS_MACOS
    QSurfaceFormat surface;
    surface.setVersion(3, 2); surface.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(surface);
#endif
    std::unique_ptr<QCoreApplication> app(setup || cliOnly ? new QCoreApplication(argc, argv) : new QApplication(argc, argv));
    QCoreApplication::setApplicationName("mpv-clipper");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCoreApplication::setOrganizationName("mpv-clipper");

    QCommandLineParser parser;
    configureLaunchParser(parser);
    parser.process(*app);
    if (cliOnly) return 0;
    if (setup) {
        try {
            if (parser.isSet("install-mpv") == parser.isSet("uninstall-mpv"))
                throw std::runtime_error("Choose install or uninstall, not both.");
            if (!parser.positionalArguments().isEmpty() || parser.isSet("source") || parser.isSet("launch-stdin") ||
                parser.isSet("launch-payload") || parser.isSet("time") || parser.isSet("wait-for-close") ||
                (parser.isSet("mpv-config") && parser.value("mpv-config").isEmpty()))
                throw std::runtime_error("Setup accepts only --mpv-config <directory>.");
            configureMpv(parser.value("mpv-config"), parser.isSet("uninstall-mpv"));
            return 0;
        } catch (const std::exception &e) { qCritical("%s", e.what()); return 1; }
    }
    // Packaged FFmpeg helpers live beside the executable on every platform.
    qputenv("PATH", (QCoreApplication::applicationDirPath() + QDir::listSeparator() + qEnvironmentVariable("PATH")).toUtf8());
    LaunchOptions options;
    QString error;
    if (!readLaunchOptions(parser, options, error)) {
        qCCritical(logApp).noquote() << error;
        return 2;
    }
    try {
        LaunchPayload payload;
        if (options.payloadStdin) {
            QFile input;
            if (!input.open(stdin, QIODevice::ReadOnly)) throw std::runtime_error("Could not read launch context.");
            payload = LaunchPayload::parse(input.read(1024 * 1024 + 1));
        }
        else if (!options.payloadFile.isEmpty()) payload = LaunchPayload::read(options.payloadFile);
        else {
            payload.source = options.source; payload.time = options.timeSec;
            payload = LaunchPayload::parse(QJsonDocument(payload.json()).toJson());
        }
        SingleInstance instance;
        bool wait = parser.isSet("wait-for-close");
        if (!instance.start(QJsonDocument(payload.json()).toJson(QJsonDocument::Compact), wait))
            return wait ? app->exec() : 0;
        MainWindow window;
        QObject::connect(&instance, &SingleInstance::received, &window, [&window](const QByteArray &bytes) {
            try { window.open(LaunchPayload::parse(bytes)); }
            catch (const std::exception &e) { QMessageBox::warning(&window, "Invalid launch request", e.what()); }
        });
        window.show();
        QTimer::singleShot(0, &window, [&] { window.open(payload); });
        qCInfo(logApp) << "Application started";
        return app->exec();
    } catch (const std::exception &e) {
        qCCritical(logApp) << e.what();
        QMessageBox::critical(nullptr, "Could not open Clipper", e.what());
        return 1;
    }
}

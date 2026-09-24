#include "LaunchOptions.h"

#include <cmath>

void configureLaunchParser(QCommandLineParser &parser)
{
    parser.setApplicationDescription("A native clipping companion for mpv (in development).");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"wait-for-close", "Keep a forwarded launcher alive until the editor closes."});
    parser.addOption({"launch-stdin", "Read launch context JSON from standard input."});
    parser.addOption({"install-mpv", "Install the mpv bridge for this user."});
    parser.addOption({"uninstall-mpv", "Remove the bridge and restore backed-up mpv files."});
    parser.addOption({"mpv-config", "Custom mpv configuration directory for installation.", "directory"});
    parser.addOptions({
        {"launch-payload", "Launch context JSON file.", "file"},
        {"source", "Local media file (development).", "file"},
        {"time", "Initial position in seconds; requires --source.", "seconds"}
    });
}

bool readLaunchOptions(const QCommandLineParser &parser, LaunchOptions &options,
                       QString &error)
{
    options = {};
    error.clear();
    if (!parser.positionalArguments().isEmpty())
        error = "Unexpected positional argument; use --source <file>.";
    else if (int(parser.isSet("launch-payload")) + int(parser.isSet("source")) + int(parser.isSet("launch-stdin")) > 1)
        error = "Use only one of --launch-payload, --launch-stdin, or --source.";
    else if (parser.isSet("time") && !parser.isSet("source"))
        error = "--time requires --source.";
    else if ((parser.isSet("source") && parser.value("source").isEmpty()) ||
             (parser.isSet("launch-payload") && parser.value("launch-payload").isEmpty()))
        error = "The input file path must not be empty.";
    if (!error.isEmpty())
        return false;

    options.payloadFile = parser.value("launch-payload");
    options.payloadStdin = parser.isSet("launch-stdin");
    options.source = parser.value("source");
    if (parser.isSet("time")) {
        bool ok = false;
        options.timeSec = parser.value("time").toDouble(&ok);
        if (!ok || !std::isfinite(options.timeSec) || options.timeSec < 0) {
            error = "--time must be a finite, non-negative number of seconds.";
            return false;
        }
    }
    return true;
}

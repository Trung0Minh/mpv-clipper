#pragma once

#include <QCommandLineParser>

struct LaunchOptions {
    QString payloadFile;
    QString source;
    bool payloadStdin = false;
    double timeSec = 0;
};

void configureLaunchParser(QCommandLineParser &parser);
bool readLaunchOptions(const QCommandLineParser &parser, LaunchOptions &options,
                       QString &error);

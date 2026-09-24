#pragma once
#include <QString>
#include <optional>
QString timecode(double seconds);
std::optional<double> parseTimecode(const QString &text);
QString safeFilename(QString text);

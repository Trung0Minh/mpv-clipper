#include "Timecode.h"
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

QString timecode(double seconds)
{
    if (!std::isfinite(seconds)) seconds = 0;
    qint64 ms = qRound64(std::clamp(seconds, 0.0, 1e9) * 1000);
    return QString("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QChar('0'))
        .arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
}

std::optional<double> parseTimecode(const QString &text)
{
    static const QRegularExpression syntax("^\\d+(?::\\d{1,2}){0,2}(?:\\.\\d{1,3})?$");
    if (!syntax.match(text.trimmed()).hasMatch()) return {};
    const auto parts = text.trimmed().split(':');
    double result = 0;
    for (int i = 0; i < parts.size(); ++i) {
        bool ok;
        double value = parts[i].toDouble(&ok);
        if (!ok || (i > 0 && value >= 60)) return {};
        result = result * 60 + value;
    }
    if (!std::isfinite(result) || result > 1e9) return {};
    return result;
}

QString safeFilename(QString text)
{
    text.replace(QRegularExpression("[\\x00-\\x1f<>:\"/\\\\|?*]"), "_");
    text = text.trimmed();
    while (text.endsWith('.') || text.endsWith(' ')) text.chop(1);
    while (text.toUtf8().size() > 180) text.chop(1);
    return text.isEmpty() ? "clip" : text;
}

#include "ExportCommandBuilder.h"
#include "media/Timecode.h"
#include <QFileInfo>
#include <QDir>
#include <cmath>
#include <filesystem>
#include <stdexcept>

bool textSubtitle(const QString &codec)
{
    return QStringList{"ass", "ssa", "subrip", "srt", "webvtt", "mov_text", "text"}.contains(codec);
}
bool Capabilities::formatSupported(const QString &f) const
{
    if (!available) return false;
    if (f == "gif") return encoders.contains("gif") && filters.contains("palettegen") && filters.contains("paletteuse");
    if (f == "webm") return encoders.contains("libvpx-vp9");
    return (f == "mp4" || f == "mkv") && encoders.contains("libx264");
}
Capabilities Capabilities::detect()
{
    Capabilities c;
    for (auto kind : {QString("encoders"), QString("filters")}) {
        QProcess p;
        p.start("ffmpeg", {"-hide_banner", "-" + kind});
        if (!p.waitForFinished(10000) || p.exitCode() != 0) {
            p.kill(); p.waitForFinished(); c.error = "FFmpeg is missing or could not report its features."; return c;
        }
        auto &set = kind == "encoders" ? c.encoders : c.filters;
        for (auto line : QString::fromUtf8(p.readAllStandardOutput()).split('\n')) {
            auto parts = line.simplified().split(' ');
            if (parts.size() > 1) set.insert(parts[1]);
        }
    }
    c.available = true; return c;
}
QString exportValidation(const MediaInfo &m, const ExportSettings &s, const Capabilities &caps)
{
    if (!QFileInfo(m.source).isFile() || !QFileInfo(m.source).isReadable()) return "The source file is unavailable.";
    if (m.video < 0 || !std::isfinite(s.start) || !std::isfinite(s.end) || s.start < 0 ||
        s.end <= s.start || s.end > m.duration + 0.001) return "Choose a valid Start and End.";
    if (!caps.available) return caps.error.isEmpty() ? "Checking FFmpeg features..." : caps.error;
    if (!caps.formatSupported(s.format)) return "This output format is unavailable in your FFmpeg installation.";
    if (s.quality < 0 || s.quality > 2 || s.fps < 0 || s.fps > 120 || s.gifWidth < 0 ||
        s.customWidth < 0 || s.customHeight < 0 || s.customWidth > 16384 || s.customHeight > 16384)
        return "Invalid output settings.";
    if ((s.customWidth == 0) != (s.customHeight == 0)) return "Set both custom dimensions.";
    if (s.audio < -1 || s.audio >= m.audio.size() || s.subtitle < -1 || s.subtitle >= m.subtitles.size())
        return "The selected track is unavailable.";
    if (s.format != "gif" && s.audio >= 0 && !caps.encoders.contains(s.format == "webm" ? "libopus" : "aac"))
        return "The audio encoder for this format is unavailable. Select None for audio.";
    if (!QStringList{"None", "Hardsub", "Softsub"}.contains(s.subtitleMode)) return "Invalid subtitle mode.";
    if (s.subtitleMode != "None") {
        if (s.subtitle < 0) return "Select a subtitle track.";
        const auto &track = m.subtitles[s.subtitle];
        if (!track.external.isEmpty() && !QFileInfo(track.external).isReadable()) return "The external subtitle file is unavailable.";
        if (!textSubtitle(track.codec)) return "This subtitle type is not supported for export. Select None.";
        if (s.subtitleMode == "Hardsub" && !caps.filters.contains("subtitles")) return "FFmpeg subtitle rendering is unavailable.";
        if (s.subtitleMode == "Softsub" && (s.format == "gif" ||
            !caps.encoders.contains(s.format == "mp4" ? "mov_text" : s.format == "webm" ? "webvtt" : "ass") ||
            !caps.encoders.contains("ass"))) return "Softsub is unavailable for this format.";
    }
    QFileInfo output(s.destination);
    if (output.fileName().isEmpty() || output.fileName().toUtf8().size() > 240 ||
        output.suffix().toLower() != s.format) return "Choose a filename with the correct extension.";
    std::error_code ec;
    if (output.absoluteFilePath() == QFileInfo(m.source).absoluteFilePath() ||
        std::filesystem::equivalent(QFileInfo(m.source).filesystemFilePath(), output.filesystemFilePath(), ec))
        return "The export must not overwrite the source video.";
    if (!output.dir().exists()) return "The output folder does not exist.";
    if (!QFileInfo(output.absolutePath()).isWritable()) return "The output folder is not writable.";
    if (output.isDir() || output.isSymLink()) return "Choose a regular output file, not a folder or symbolic link.";
    return {};
}

static QString number(double value) { return QString::number(value, 'f', 6); }
QStringList exportArguments(const MediaInfo &m, const ExportSettings &s,
                            const QString &output, const QString &subtitleInput)
{
    if (!QStringList{"mp4", "mkv", "webm", "gif"}.contains(s.format) ||
        !std::isfinite(s.start) || !std::isfinite(s.end) || s.start < 0 || s.end <= s.start ||
        s.end > m.duration + 0.001 || m.video < 0 || s.quality < 0 || s.quality > 2 ||
        s.audio < -1 || s.audio >= m.audio.size() || s.subtitle < -1 || s.subtitle >= m.subtitles.size() ||
        !QStringList{"None", "Hardsub", "Softsub"}.contains(s.subtitleMode) ||
        (s.subtitleMode != "None" && (s.subtitle < 0 || !textSubtitle(m.subtitles[s.subtitle].codec))) ||
        (s.subtitleMode == "Softsub" && (s.format == "gif" || subtitleInput.isEmpty())))
        throw std::runtime_error("Invalid export settings or unsupported track combination.");
    QStringList args{"-hide_banner", "-nostdin", "-y", "-progress", "pipe:1", "-nostats"};
    // Keep source-relative timestamps for trimming and animated subtitles after seeking.
    // A short preroll lets decoding settle without walking the whole source.
    if (s.start >= 3)
        args << "-copyts" << "-start_at_zero" << "-ss" << number(std::floor(s.start) - 2) << "-noaccurate_seek";
    args << "-i" << m.source;
    if (s.subtitleMode == "Softsub") args << "-i" << subtitleInput;
    QStringList filters;
    if (s.subtitleMode == "Hardsub") {
        if (subtitleInput.isEmpty() || subtitleInput.contains(QChar::Null))
            throw std::runtime_error("Invalid subtitle path.");
        // Escape the option parser, then the filtergraph parser (no shell involved).
        auto escape = [](const QString &value, const QString &special) {
            QString result;
            for (QChar c : value) { if (special.contains(c)) result += '\\'; result += c; }
            return result;
        };
        const auto filename = escape(escape(subtitleInput, "\\': \t\r\n"), "\\'[],; \t\r\n");
        const auto &track = m.subtitles.at(s.subtitle);
        filters << QString("subtitles=filename=%1:si=%2").arg(filename).arg(track.external.isEmpty() ? track.ordinal : 0);
    }
    filters << QString("trim=start=%1:end=%2").arg(number(s.start), number(s.end)) << "setpts=PTS-STARTPTS";
    if (s.fps) filters << QString("fps=%1").arg(s.fps);
    if (s.format == "gif") {
        filters << (s.gifWidth ? QString("scale=w='min(iw,%1)':h=-1:flags=lanczos").arg(s.gifWidth) : "scale=iw:-1:flags=lanczos");
    } else if (s.customWidth && s.customHeight) {
        filters << QString("scale=%1:%2,setsar=1").arg(std::max(2, s.customWidth / 2 * 2)).arg(std::max(2, s.customHeight / 2 * 2));
    } else {
        filters << (s.height ? QString("scale=w=-2:h='max(2,trunc(min(ih,%1)/2)*2)'").arg(s.height)
                             : "scale=w='max(2,trunc(iw/2)*2)':h='max(2,trunc(ih/2)*2)'");
    }
    QString graph = QString("[0:%1]%2").arg(m.video).arg(filters.join(','));
    if (s.format == "gif") {
        int colors[] = {256, 192, 128};
        graph += QString(",split[a][b];[a]palettegen=max_colors=%1[p];[b][p]paletteuse=dither=%2[v]")
            .arg(colors[s.quality]).arg(s.quality == 2 ? "bayer" : "sierra2_4a");
    } else graph += "[v]";
    if (s.format != "gif" && s.audio >= 0)
        graph += QString(";[0:%1]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS[audio]")
            .arg(m.audio[s.audio].index).arg(number(s.start), number(s.end));
    args << "-filter_complex_threads" << "1" << "-filter_complex" << graph << "-map" << "[v]";
    if (s.format != "gif" && s.audio >= 0)
        args << "-map" << "[audio]" << "-c:a" << (s.format == "webm" ? "libopus" : "aac") << "-b:a" << "192k";
    else args << "-an";
    if (s.subtitleMode == "Softsub") args << "-map" << "1:0" << "-c:s" <<
        (s.format == "mp4" ? "mov_text" : s.format == "webm" ? "webvtt" : "ass");
    else args << "-sn";
    if (s.format != "gif") {
        args << "-c:v" << (s.format == "webm" ? "libvpx-vp9" : "libx264") << "-pix_fmt" << "yuv420p";
        args << "-crf" << QString::number((s.format == "webm" ? 24 : 18) + s.quality * 5);
        if (s.format == "webm") args << "-b:v" << "0" << "-deadline" << "good" << "-cpu-used" << "2";
        else args << "-preset" << "medium";
    } else args << "-loop" << "0";
    if (s.format == "mp4") args << "-movflags" << "+faststart";
    args << "-fps_mode" << "vfr" << "-t" << number(s.end - s.start)
         << "-map_metadata" << "-1" << "-map_chapters" << "-1" << output;
    return args;
}

QByteArray trimAss(const QByteArray &ass, double start, double end)
{
    QByteArray result;
    for (auto line : QString::fromUtf8(ass).split('\n')) {
        if (line.startsWith("Dialogue:")) {
            auto fields = line.split(',');
            if (fields.size() < 10) throw std::runtime_error("Invalid subtitle dialogue.");
            auto a = parseTimecode(fields[1]), b = parseTimecode(fields[2]);
            if (!a || !b) throw std::runtime_error("Invalid subtitle timing.");
            if (*b <= start || *a >= end) continue;
            fields[1] = timecode(std::max(0.0, *a - start)).chopped(1);
            fields[2] = timecode(std::min(end, *b) - start).chopped(1);
            line = fields.join(',');
        }
        result += line.toUtf8() + '\n';
    }
    return result;
}

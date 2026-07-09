#include "DownloadFormatPredictor.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <sstream>

namespace
{
    std::string Trim(std::string value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        {
            value.pop_back();
        }
        return value;
    }

    std::string ToLower(std::string value)
    {
        for (char& c : value)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return value;
    }

    std::string ToUpper(std::string value)
    {
        for (char& c : value)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return value;
    }

    std::vector<std::string> SplitCommaSeparated(const std::string& value)
    {
        std::vector<std::string> items;
        std::stringstream stream(value);
        std::string item;
        while (std::getline(stream, item, ','))
        {
            items.push_back(Trim(item));
        }
        return items;
    }

    bool IsMissingCodec(const std::string& codec)
    {
        return codec.empty() || codec == "none" || codec == "None" || codec == "NA";
    }

    bool HasVideo(const LinkFormatStream& stream)
    {
        return !IsMissingCodec(stream.vcodec);
    }

    bool HasAudio(const LinkFormatStream& stream)
    {
        return !IsMissingCodec(stream.acodec);
    }

    bool IsMuxed(const LinkFormatStream& stream)
    {
        return HasVideo(stream) && HasAudio(stream);
    }

    bool IsAudioOnly(const LinkFormatStream& stream)
    {
        return !HasVideo(stream) && HasAudio(stream);
    }

    bool IsVideoOnly(const LinkFormatStream& stream)
    {
        return HasVideo(stream) && !HasAudio(stream);
    }

    bool IsBlockedMp4Stream(const LinkFormatStream& stream)
    {
        return stream.formatId.find("-sr") != std::string::npos;
    }

    bool AcceptVideoStream(const LinkFormatStream& stream, const std::string& ext)
    {
        return ToLower(ext) != "mp4" || !IsBlockedMp4Stream(stream);
    }

    std::string NormalizeCodecName(std::string value)
    {
        value = Trim(value);
        if (IsMissingCodec(value))
        {
            return "None";
        }

        const std::string lower = ToLower(value);
        if (lower.rfind("avc1", 0) == 0 || lower.rfind("h264", 0) == 0)
        {
            return "H.264";
        }
        if (lower.rfind("hev1", 0) == 0 || lower.rfind("hvc1", 0) == 0 || lower.rfind("hevc", 0) == 0 || lower.rfind("h265", 0) == 0)
        {
            return "H.265";
        }
        if (lower.rfind("av01", 0) == 0 || lower.rfind("av1", 0) == 0)
        {
            return "AV1";
        }
        if (lower.rfind("vp09", 0) == 0 || lower.rfind("vp9", 0) == 0)
        {
            return "VP9";
        }
        if (lower.rfind("mp4a", 0) == 0 || lower.rfind("aac", 0) == 0)
        {
            return "AAC";
        }
        if (lower == "opus")
        {
            return "Opus";
        }
        if (lower == "mp3")
        {
            return "MP3";
        }
        return value;
    }

    bool MatchesAudioTarget(const LinkFormatStream& stream, const std::string& targetExt)
    {
        const std::string ext = ToLower(stream.ext);
        const std::string codec = ToLower(stream.acodec);
        if (ext == targetExt)
        {
            return true;
        }

        if (targetExt == "m4a")
        {
            return ext == "mp4" || codec.find("mp4a") != std::string::npos || codec.find("aac") != std::string::npos;
        }

        return false;
    }

    int VideoCodecRank(const std::string& vcodec)
    {
        const std::string normalized = NormalizeCodecName(vcodec);
        if (normalized == "AV1")
        {
            return 5;
        }
        if (normalized == "H.265")
        {
            return 4;
        }
        if (normalized == "VP9")
        {
            return 3;
        }
        if (normalized == "H.264")
        {
            return 2;
        }
        return 1;
    }

    bool IsBetterVideoCandidate(const LinkFormatStream& candidate, const LinkFormatStream* current)
    {
        if (current == nullptr)
        {
            return true;
        }

        if (candidate.height != current->height)
        {
            return candidate.height > current->height;
        }

        if (VideoCodecRank(candidate.vcodec) != VideoCodecRank(current->vcodec))
        {
            return VideoCodecRank(candidate.vcodec) > VideoCodecRank(current->vcodec);
        }

        return false;
    }

    int ParseMaxHeight(const std::string& quality)
    {
        if (quality.empty())
        {
            return INT_MAX;
        }

        int height = 0;
        for (const char c : quality)
        {
            if (c >= '0' && c <= '9')
            {
                height = height * 10 + (c - '0');
            }
        }
        return height > 0 ? height : INT_MAX;
    }

    bool MatchesHeight(int height, const std::string& quality)
    {
        if (height < 360)
        {
            return false;
        }
        return height <= ParseMaxHeight(quality);
    }

    std::string FormatResolution(int height)
    {
        if (height <= 0)
        {
            return {};
        }
        return std::to_string(height) + "p";
    }

    const LinkFormatStream* PickBestVideoOnly(
        const std::vector<LinkFormatStream>& streams,
        const std::string& ext,
        const std::string& quality)
    {
        const std::string targetExt = ToLower(ext);
        const LinkFormatStream* best = nullptr;
        for (const LinkFormatStream& stream : streams)
        {
            if (ToLower(stream.ext) != targetExt || !IsVideoOnly(stream) || !MatchesHeight(stream.height, quality) ||
                !AcceptVideoStream(stream, ext))
            {
                continue;
            }
            if (!IsBetterVideoCandidate(stream, best))
            {
                continue;
            }
            best = &stream;
        }
        return best;
    }

    const LinkFormatStream* PickBestMuxed(
        const std::vector<LinkFormatStream>& streams,
        const std::string& ext,
        const std::string& quality)
    {
        const std::string targetExt = ToLower(ext);
        const LinkFormatStream* best = nullptr;
        for (const LinkFormatStream& stream : streams)
        {
            if (ToLower(stream.ext) != targetExt || !IsMuxed(stream) || !MatchesHeight(stream.height, quality) ||
                !AcceptVideoStream(stream, ext))
            {
                continue;
            }
            if (!IsBetterVideoCandidate(stream, best))
            {
                continue;
            }
            best = &stream;
        }
        return best;
    }

    const LinkFormatStream* PickBestAudioOnly(
        const std::vector<LinkFormatStream>& streams,
        const std::string& ext)
    {
        const std::string targetExt = ToLower(ext);
        const LinkFormatStream* best = nullptr;
        for (const LinkFormatStream& stream : streams)
        {
            if (!IsAudioOnly(stream) || !MatchesAudioTarget(stream, targetExt))
            {
                continue;
            }
            best = &stream;
        }
        return best;
    }

    PredictedDownload MakePrediction(
        const std::string& container,
        const std::string& videoCodec,
        const std::string& audioCodec,
        int height)
    {
        PredictedDownload prediction;
        prediction.container = ToUpper(container);
        prediction.videoCodec = videoCodec;
        prediction.audioCodec = audioCodec;
        prediction.resolution = FormatResolution(height);
        return prediction;
    }

    PredictedDownload PredictBoth(
        const std::vector<LinkFormatStream>& streams,
        const std::string& ext,
        const std::string& quality)
    {
        const std::string lowerExt = ToLower(ext);
        const std::string audioExt = lowerExt == "mp4" ? "m4a" : lowerExt;

        const LinkFormatStream* video = PickBestVideoOnly(streams, lowerExt, quality);
        const LinkFormatStream* audio = PickBestAudioOnly(streams, audioExt);
        if (video != nullptr && audio != nullptr)
        {
            return MakePrediction(
                lowerExt,
                NormalizeCodecName(video->vcodec),
                NormalizeCodecName(audio->acodec),
                video->height);
        }

        const LinkFormatStream* muxed = PickBestMuxed(streams, lowerExt, quality);
        if (muxed != nullptr)
        {
            return MakePrediction(
                lowerExt,
                NormalizeCodecName(muxed->vcodec),
                NormalizeCodecName(muxed->acodec),
                muxed->height);
        }

        if (video != nullptr)
        {
            return MakePrediction(
                lowerExt,
                NormalizeCodecName(video->vcodec),
                "None",
                video->height);
        }

        return MakePrediction(ext, "Unknown", "Unknown", 0);
    }
}

std::vector<LinkFormatStream> ParseLinkFormatStreams(
    const std::string& commaSeparatedFormatIds,
    const std::string& commaSeparatedExts,
    const std::string& commaSeparatedHeights,
    const std::string& commaSeparatedVideoCodecs,
    const std::string& commaSeparatedAudioCodecs)
{
    const std::vector<std::string> formatIds = SplitCommaSeparated(commaSeparatedFormatIds);
    const std::vector<std::string> exts = SplitCommaSeparated(commaSeparatedExts);
    const std::vector<std::string> heights = SplitCommaSeparated(commaSeparatedHeights);
    const std::vector<std::string> videoCodecs = SplitCommaSeparated(commaSeparatedVideoCodecs);
    const std::vector<std::string> audioCodecs = SplitCommaSeparated(commaSeparatedAudioCodecs);

    std::vector<LinkFormatStream> streams;
    const size_t count = exts.size();
    streams.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        std::string ext = ToLower(exts[index]);
        if (ext.empty() || ext == "mhtml")
        {
            continue;
        }

        LinkFormatStream stream;
        stream.formatId = index < formatIds.size() ? formatIds[index] : "";
        stream.ext = ext;
        if (index < heights.size())
        {
            try
            {
                stream.height = std::stoi(heights[index]);
            }
            catch (...)
            {
                stream.height = 0;
            }
        }
        stream.vcodec = index < videoCodecs.size() ? videoCodecs[index] : "";
        stream.acodec = index < audioCodecs.size() ? audioCodecs[index] : "";
        streams.push_back(stream);
    }

    return streams;
}

PredictedDownload PredictDownload(
    const std::vector<LinkFormatStream>& streams,
    const std::vector<std::string>& videoFormats,
    const std::vector<std::string>& audioFormats,
    const std::vector<std::string>& qualities,
    const DownloadOptions& options)
{
    const std::vector<std::string> mediaModes = {"Both", "Video only", "Audio only"};
    const std::vector<std::string>& availableFormats = options.mediaMode == 2 ? audioFormats : videoFormats;

    if (availableFormats.empty() || streams.empty())
    {
        return MakePrediction("Unknown", "Unknown", "Unknown", 0);
    }

    const int formatIndex = std::clamp(options.fileFormat, 0, static_cast<int>(availableFormats.size()) - 1);
    const int mediaIndex = std::clamp(options.mediaMode, 0, static_cast<int>(mediaModes.size()) - 1);
    const int qualityIndex = qualities.empty()
        ? 0
        : std::clamp(options.quality, 0, static_cast<int>(qualities.size()) - 1);

    const std::string fileFormat = availableFormats[formatIndex];
    const std::string mediaMode = mediaModes[mediaIndex];
    const std::string quality = qualities.empty() ? "2160p" : qualities[qualityIndex];
    const std::string ext = ToLower(fileFormat);

    if (mediaMode == "Audio only")
    {
        const LinkFormatStream* audio = PickBestAudioOnly(streams, ext);
        if (audio != nullptr)
        {
            return MakePrediction(ext, "None", NormalizeCodecName(audio->acodec), 0);
        }
        return MakePrediction(fileFormat, "None", "Unknown", 0);
    }

    if (mediaMode == "Video only")
    {
        const LinkFormatStream* video = PickBestVideoOnly(streams, ext, quality);
        if (video != nullptr)
        {
            return MakePrediction(ext, NormalizeCodecName(video->vcodec), "None", video->height);
        }

        const LinkFormatStream* muxed = PickBestMuxed(streams, ext, quality);
        if (muxed != nullptr)
        {
            return MakePrediction(ext, NormalizeCodecName(muxed->vcodec), "None", muxed->height);
        }

        return MakePrediction(fileFormat, "Unknown", "None", 0);
    }

    return PredictBoth(streams, ext, quality);
}

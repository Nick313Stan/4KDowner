#include "DownloadFormatPredictor.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
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
    if (lower.rfind("hev1", 0) == 0 || lower.rfind("hvc1", 0) == 0 || lower.rfind("hevc", 0) == 0 ||
        lower.rfind("h265", 0) == 0)
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

bool MatchesSelectedQuality(int height, const std::string& quality)
{
    if (height < 144)
    {
        return false;
    }

    const int selected = ParseQualityHeight(quality);
    if (selected <= 0)
    {
        return true;
    }

    // Cap semantics: accept any stream at or below the selected height.
    return BucketDownloadHeight(height) <= selected;
}

std::string FormatResolution(int height)
{
    if (height <= 0)
    {
        return {};
    }
    return std::to_string(height) + "p";
}

const LinkFormatStream*
PickBestVideoOnly(const std::vector<LinkFormatStream>& streams, const std::string& ext, const std::string& quality)
{
    const std::string targetExt = ToLower(ext);
    const LinkFormatStream* best = nullptr;
    for (const LinkFormatStream& stream : streams)
    {
        if (ToLower(stream.ext) != targetExt || !IsVideoOnly(stream) ||
            !MatchesSelectedQuality(stream.height, quality) || !AcceptVideoStream(stream, ext))
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

const LinkFormatStream*
PickBestMuxed(const std::vector<LinkFormatStream>& streams, const std::string& ext, const std::string& quality)
{
    const std::string targetExt = ToLower(ext);
    const LinkFormatStream* best = nullptr;
    for (const LinkFormatStream& stream : streams)
    {
        if (ToLower(stream.ext) != targetExt || !IsMuxed(stream) || !MatchesSelectedQuality(stream.height, quality) ||
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

const LinkFormatStream* PickBestAudioOnly(const std::vector<LinkFormatStream>& streams, const std::string& ext)
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

PredictedDownload
MakePrediction(const std::string& container, const std::string& videoCodec, const std::string& audioCodec, int height)
{
    PredictedDownload prediction;
    prediction.container = ToUpper(container);
    prediction.videoCodec = videoCodec;
    prediction.audioCodec = audioCodec;
    prediction.resolution = FormatResolution(height);
    return prediction;
}

PredictedDownload
PredictBoth(const std::vector<LinkFormatStream>& streams, const std::string& ext, const std::string& quality)
{
    const std::string lowerExt = ToLower(ext);
    const std::string audioExt = lowerExt == "mp4" ? "m4a" : lowerExt;

    const LinkFormatStream* video = PickBestVideoOnly(streams, lowerExt, quality);
    const LinkFormatStream* audio = PickBestAudioOnly(streams, audioExt);
    if (video != nullptr && audio != nullptr)
    {
        return MakePrediction(
            lowerExt, NormalizeCodecName(video->vcodec), NormalizeCodecName(audio->acodec), video->height);
    }

    const LinkFormatStream* muxed = PickBestMuxed(streams, lowerExt, quality);
    if (muxed != nullptr)
    {
        return MakePrediction(
            lowerExt, NormalizeCodecName(muxed->vcodec), NormalizeCodecName(muxed->acodec), muxed->height);
    }

    if (video != nullptr)
    {
        return MakePrediction(lowerExt, NormalizeCodecName(video->vcodec), "None", video->height);
    }

    return MakePrediction(ext, "Unknown", "Unknown", 0);
}
} // namespace

std::vector<LinkFormatStream> ParseLinkFormatStreams(const std::string& commaSeparatedFormatIds,
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

namespace
{
std::string ExtractJsonFieldRaw(const std::string& object, const char* key)
{
    const std::string pattern = std::string("\"") + key + "\":";
    size_t pos = object.find(pattern);
    if (pos == std::string::npos)
    {
        return {};
    }
    pos += pattern.size();
    while (pos < object.size() && (object[pos] == ' ' || object[pos] == '\t'))
    {
        ++pos;
    }
    if (pos >= object.size() || object.compare(pos, 4, "null") == 0)
    {
        return {};
    }

    if (object[pos] == '"')
    {
        ++pos;
        std::string value;
        while (pos < object.size() && object[pos] != '"')
        {
            if (object[pos] == '\\' && pos + 1 < object.size())
            {
                value.push_back(object[pos + 1]);
                pos += 2;
                continue;
            }
            value.push_back(object[pos]);
            ++pos;
        }
        return value;
    }

    const size_t end = object.find_first_of(",}", pos);
    return Trim(object.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

int ExtractJsonIntField(const std::string& object, const char* key)
{
    const std::string raw = ExtractJsonFieldRaw(object, key);
    if (raw.empty() || raw == "null" || raw == "NA" || raw == "None" || raw == "none")
    {
        return 0;
    }
    try
    {
        return std::stoi(raw);
    }
    catch (...)
    {
        return 0;
    }
}

int HeightFromResolutionString(const std::string& resolution)
{
    // "7680x4320" / "7680×4320"
    const size_t sep = resolution.find_first_of("xX×");
    if (sep == std::string::npos || sep + 1 >= resolution.size())
    {
        return 0;
    }
    try
    {
        return std::stoi(resolution.substr(sep + 1));
    }
    catch (...)
    {
        return 0;
    }
}

int HeightFromFormatNote(const std::string& note)
{
    // Prefer explicit ladder tokens (4320p, 2160p, ...) over incidental digits.
    static const int kLadder[] = {4320, 2160, 1440, 1080, 720, 480, 360, 240, 144};
    for (const int height : kLadder)
    {
        const std::string token = std::to_string(height) + "p";
        if (ToLower(note).find(token) != std::string::npos)
        {
            return height;
        }
    }
    return 0;
}

int HeightFromWidthOnly(int width)
{
    // Common YouTube landscape widths when height is missing from the print template.
    if (width >= 7680)
    {
        return 4320;
    }
    if (width >= 3840)
    {
        return 2160;
    }
    if (width >= 2560)
    {
        return 1440;
    }
    if (width >= 1920)
    {
        return 1080;
    }
    if (width >= 1280)
    {
        return 720;
    }
    if (width >= 854)
    {
        return 480;
    }
    if (width >= 640)
    {
        return 360;
    }
    if (width >= 426)
    {
        return 240;
    }
    if (width >= 256)
    {
        return 144;
    }
    return 0;
}

int InferStreamHeight(const std::string& object)
{
    const int height = ExtractJsonIntField(object, "height");
    if (height > 0)
    {
        return height;
    }

    const int fromResolution = HeightFromResolutionString(ExtractJsonFieldRaw(object, "resolution"));
    if (fromResolution > 0)
    {
        return fromResolution;
    }

    const int fromNote = HeightFromFormatNote(ExtractJsonFieldRaw(object, "format_note"));
    if (fromNote > 0)
    {
        return fromNote;
    }

    return HeightFromWidthOnly(ExtractJsonIntField(object, "width"));
}

std::vector<std::string> OrderExts(const std::set<std::string>& seenIn, const std::vector<std::string>& order)
{
    std::set<std::string> seen = seenIn;
    std::vector<std::string> result;
    for (const std::string& preferred : order)
    {
        if (seen.erase(preferred) > 0)
        {
            result.push_back(preferred);
        }
    }
    for (const std::string& ext : seen)
    {
        result.push_back(ext);
    }
    return result;
}
} // namespace

std::vector<LinkFormatStream> ParseLinkFormatStreamsJson(const std::string& jsonArray)
{
    std::vector<LinkFormatStream> streams;
    size_t cursor = 0;
    while (cursor < jsonArray.size())
    {
        const size_t start = jsonArray.find('{', cursor);
        if (start == std::string::npos)
        {
            break;
        }
        const size_t end = jsonArray.find('}', start);
        if (end == std::string::npos)
        {
            break;
        }

        const std::string object = jsonArray.substr(start, end - start + 1);
        cursor = end + 1;

        std::string ext = ToLower(ExtractJsonFieldRaw(object, "ext"));
        if (ext.empty() || ext == "mhtml")
        {
            continue;
        }

        LinkFormatStream stream;
        stream.formatId = ExtractJsonFieldRaw(object, "format_id");
        stream.ext = std::move(ext);
        stream.height = InferStreamHeight(object);
        stream.vcodec = ExtractJsonFieldRaw(object, "vcodec");
        stream.acodec = ExtractJsonFieldRaw(object, "acodec");
        streams.push_back(std::move(stream));
    }
    return streams;
}

std::vector<std::string> FormatsFromStreams(const std::vector<LinkFormatStream>& streams)
{
    std::set<std::string> seen;
    for (const LinkFormatStream& stream : streams)
    {
        std::string ext = ToUpper(stream.ext);
        if (!ext.empty() && ext != "MHTML")
        {
            seen.insert(std::move(ext));
        }
    }

    std::vector<std::string> preferred =
        OrderExts(seen, {"MP4", "WEBM", "M4A", "MKV", "MP3", "OPUS", "AAC", "WAV", "FLAC"});
    if (preferred.empty())
    {
        preferred.push_back("MP4");
    }
    return preferred;
}

std::vector<std::string> VideoFormatsFromStreams(const std::vector<LinkFormatStream>& streams)
{
    std::set<std::string> seen;
    for (const LinkFormatStream& stream : streams)
    {
        if (!HasVideo(stream))
        {
            continue;
        }
        std::string ext = ToUpper(stream.ext);
        if (!ext.empty() && ext != "MHTML")
        {
            seen.insert(std::move(ext));
        }
    }

    std::vector<std::string> result = OrderExts(seen, {"MP4", "MKV", "WEBM"});
    if (result.empty())
    {
        result.push_back("MP4");
    }
    return result;
}

std::vector<std::string> AudioFormatsFromStreams(const std::vector<LinkFormatStream>& streams)
{
    std::set<std::string> seen;
    for (const LinkFormatStream& stream : streams)
    {
        if (!IsAudioOnly(stream))
        {
            continue;
        }
        std::string ext = ToUpper(stream.ext);
        if (!ext.empty() && ext != "MHTML")
        {
            seen.insert(std::move(ext));
        }
    }

    std::vector<std::string> result = OrderExts(seen, {"M4A", "MP3", "WEBM", "OPUS", "AAC", "WAV", "FLAC"});
    if (std::find(result.begin(), result.end(), "MP3") == result.end())
    {
        result.push_back("MP3");
    }
    if (result.empty())
    {
        result.push_back("M4A");
    }
    return result;
}

std::vector<std::string> QualitiesFromStreams(const std::vector<LinkFormatStream>& streams)
{
    std::set<int, std::greater<int>> heights;
    for (const LinkFormatStream& stream : streams)
    {
        if (!HasVideo(stream))
        {
            continue;
        }
        const int bucket = BucketDownloadHeight(stream.height);
        if (bucket > 0)
        {
            heights.insert(bucket);
        }
    }

    std::vector<std::string> qualities;
    qualities.reserve(heights.size());
    for (const int height : heights)
    {
        qualities.push_back(std::to_string(height) + "p");
    }
    return qualities;
}

PredictedDownload PredictDownload(const std::vector<LinkFormatStream>& streams,
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
    const int qualityIndex =
        qualities.empty() ? 0 : std::clamp(options.quality, 0, static_cast<int>(qualities.size()) - 1);

    const std::string fileFormat = StripFormatItemLabel(availableFormats[formatIndex]);
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

int ParseQualityHeight(const std::string& quality)
{
    if (quality.empty())
    {
        return 0;
    }

    int height = 0;
    for (const char c : quality)
    {
        if (c >= '0' && c <= '9')
        {
            height = height * 10 + (c - '0');
        }
        else if (height > 0)
        {
            break;
        }
    }
    return height;
}

int BucketDownloadHeight(int height)
{
    if (height >= 4320)
    {
        return 4320;
    }
    if (height >= 2160)
    {
        return 2160;
    }
    if (height >= 1440)
    {
        return 1440;
    }
    if (height >= 1080)
    {
        return 1080;
    }
    if (height >= 720)
    {
        return 720;
    }
    if (height >= 480)
    {
        return 480;
    }
    if (height >= 360)
    {
        return 360;
    }
    if (height >= 240)
    {
        return 240;
    }
    if (height >= 144)
    {
        return 144;
    }
    return 0;
}

bool ContainerSupportsQuality(const std::vector<LinkFormatStream>& streams, const std::string& container, int height)
{
    if (height <= 0 || container.empty())
    {
        return false;
    }

    const std::string targetExt = ToLower(container);
    for (const LinkFormatStream& stream : streams)
    {
        if (BucketDownloadHeight(stream.height) != height || ToLower(stream.ext) != targetExt)
        {
            continue;
        }
        if (!HasVideo(stream) || !AcceptVideoStream(stream, targetExt))
        {
            continue;
        }
        return true;
    }
    return false;
}

std::string StripFormatItemLabel(std::string label)
{
    auto stripSuffix = [&](const char* suffix)
    {
        const size_t suffixLen = std::strlen(suffix);
        if (label.size() >= suffixLen && label.compare(label.size() - suffixLen, suffixLen, suffix) == 0)
        {
            label.erase(label.size() - suffixLen);
            return true;
        }
        return false;
    };

    (void)(stripSuffix(" (Unavailable)") || stripSuffix(" (Current)"));
    return label;
}

bool IsInactiveFormatItem(const std::string& label)
{
    if (label.size() >= 14 && label.compare(label.size() - 14, 14, " (Unavailable)") == 0)
    {
        return true;
    }
    return label.size() >= 10 && label.compare(label.size() - 10, 10, " (Current)") == 0;
}

int FirstActiveFormatIndex(const std::vector<std::string>& items)
{
    for (int index = 0; index < static_cast<int>(items.size()); ++index)
    {
        if (!IsInactiveFormatItem(items[index]))
        {
            return index;
        }
    }
    return 0;
}

std::vector<std::string> BuildFormatItemsForQuality(const std::vector<std::string>& allFormats,
                                                    const std::vector<LinkFormatStream>& streams,
                                                    const std::string& quality,
                                                    int mediaMode)
{
    if (mediaMode == 2)
    {
        return allFormats;
    }

    const int height = ParseQualityHeight(quality);
    std::vector<std::string> items;
    items.reserve(allFormats.size());
    for (const std::string& format : allFormats)
    {
        if (height <= 0 || ContainerSupportsQuality(streams, format, height))
        {
            items.push_back(format);
        }
        else
        {
            items.push_back(format + " (Unavailable)");
        }
    }
    return items;
}

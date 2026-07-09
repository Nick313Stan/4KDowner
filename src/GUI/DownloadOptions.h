#pragma once

#include <string>

struct AutoConvertOptions
{
    bool enabled = false;
    bool convertContainer = false;
    bool convertVideo = false;
    bool convertAudio = false;
    int containerIndex = 0;
    int videoIndex = 0;
    int audioIndex = 0;

    bool IsActive() const
    {
        return enabled && (convertContainer || convertVideo || convertAudio);
    }
};

struct DownloadOptions
{
    int fileFormat = 0;
    int mediaMode = 0;
    int quality = 0;
    // Optional max-height cap ("1080p", "1440p", ...). Used when the card has no ladder yet
    // or when options came from a playlist/channel group selection.
    std::string qualityCap;
    bool useCustomPath = false;
    std::string customPath;
};

struct ConverterOptions
{
    bool convertContainer = false;
    bool convertVideo = false;
    bool convertAudio = false;
    int containerIndex = 0;
    int videoIndex = 0;
    int audioIndex = 0;

    bool IsActive() const
    {
        return convertContainer || convertVideo || convertAudio;
    }

    bool operator==(const ConverterOptions& other) const
    {
        return convertContainer == other.convertContainer && convertVideo == other.convertVideo &&
               convertAudio == other.convertAudio && containerIndex == other.containerIndex &&
               videoIndex == other.videoIndex && audioIndex == other.audioIndex;
    }

    bool operator!=(const ConverterOptions& other) const
    {
        return !(*this == other);
    }
};

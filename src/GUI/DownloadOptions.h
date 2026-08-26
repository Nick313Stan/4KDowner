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
    // Prefix downloaded filenames with the list/tab index (1. Title…).
    // For channel groups: header flag applies to all tabs; each tab can also enable it alone.
    // Default off for single videos; playlist/channel groups turn this on in their constructor.
    bool keepNumbering = false;
    // When keepNumbering is on:
    // - Channels: unchecked = N…1 (newest/top = highest), checked = 1…N.
    // - Playlists: unchecked = 1…N (top = first), checked = N…1.
    // Controls filename prefixes and list badges.
    bool inverseNumbering = false;
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

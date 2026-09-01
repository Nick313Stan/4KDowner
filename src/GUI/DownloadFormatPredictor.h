#pragma once

#include "DownloadOptions.h"

#include <cstdint>
#include <string>
#include <vector>

struct LinkFormatStream
{
    std::string formatId;
    std::string ext;
    int height = 0;
    int width = 0;
    std::int64_t filesize = 0;
    std::int64_t filesizeApprox = 0;
    std::string vcodec;
    std::string acodec;
    std::string protocol;
};

struct PredictedDownload
{
    std::string container;
    std::string videoCodec;
    std::string audioCodec;
    std::string resolution;
    bool converting = false;
};

std::vector<LinkFormatStream> ParseLinkFormatStreams(const std::string& commaSeparatedFormatIds,
                                                     const std::string& commaSeparatedExts,
                                                     const std::string& commaSeparatedHeights,
                                                     const std::string& commaSeparatedVideoCodecs,
                                                     const std::string& commaSeparatedAudioCodecs);
// Prefer this: yt-dlp %(formats.:.height)l drops None heights and misaligns parallel lists.
std::vector<LinkFormatStream> ParseLinkFormatStreamsJson(const std::string& jsonArray);

std::vector<std::string> FormatsFromStreams(const std::vector<LinkFormatStream>& streams);
std::vector<std::string> VideoFormatsFromStreams(const std::vector<LinkFormatStream>& streams);
std::vector<std::string> AudioFormatsFromStreams(const std::vector<LinkFormatStream>& streams);
std::vector<std::string> QualitiesFromStreams(const std::vector<LinkFormatStream>& streams);

PredictedDownload PredictDownload(const std::vector<LinkFormatStream>& streams,
                                  const std::vector<std::string>& videoFormats,
                                  const std::vector<std::string>& audioFormats,
                                  const std::vector<std::string>& qualities,
                                  const DownloadOptions& options);

// Best-effort total bytes for the download that matches fileFormat/mediaMode/quality.
// Uses filesize, then filesize_approx. Returns 0 when unknown.
std::int64_t EstimateDownloadBytes(const std::vector<LinkFormatStream>& streams,
                                   const std::string& fileFormat,
                                   const std::string& mediaMode,
                                   const std::string& quality);

// True when disk progress should track one main Title.ext.part (HLS / muxed / progressive).
// False when separate video+audio DASH streams produce Title.fNNN.* artifacts.
bool PredictSingleMainPartDiskProgress(const std::vector<LinkFormatStream>& streams,
                                       const std::string& fileFormat,
                                       const std::string& mediaMode,
                                       const std::string& quality);

// Rough total bitrate (video+audio) for live catch-up progress from bytes on disk.
double EstimateLiveCatchupBitrateBps(const std::string& quality, const std::string& mediaMode);

struct ResolvedDownloadQuality
{
    std::string cap;
    std::string floor;
};

ResolvedDownloadQuality ResolveDownloadQuality(const std::vector<std::string>& availableQualities,
                                               const std::string& qualityCap,
                                               int cardQualityIndex);

int ParseQualityHeight(const std::string& quality);
// Map a raw stream height to the standard download ladder (144/240/.../4320).
int BucketDownloadHeight(int height);
// YouTube "1080p" for Shorts is the short side (1080x1920 → 1080), not frame height.
int EffectiveQualityHeight(const LinkFormatStream& stream);
bool ContainerSupportsQuality(const std::vector<LinkFormatStream>& streams, const std::string& container, int height);
std::vector<std::string> BuildFormatItemsForQuality(const std::vector<std::string>& allFormats,
                                                    const std::vector<LinkFormatStream>& streams,
                                                    const std::string& quality,
                                                    int mediaMode);
std::string StripFormatItemLabel(std::string label);
bool IsInactiveFormatItem(const std::string& label);
int FirstActiveFormatIndex(const std::vector<std::string>& items);

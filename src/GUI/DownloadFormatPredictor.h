#pragma once

#include "DownloadOptions.h"

#include <string>
#include <vector>

struct LinkFormatStream
{
    std::string formatId;
    std::string ext;
    int height = 0;
    std::string vcodec;
    std::string acodec;
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

int ParseQualityHeight(const std::string& quality);
// Map a raw stream height to the standard download ladder (144/240/.../4320).
int BucketDownloadHeight(int height);
bool ContainerSupportsQuality(const std::vector<LinkFormatStream>& streams, const std::string& container, int height);
std::vector<std::string> BuildFormatItemsForQuality(const std::vector<std::string>& allFormats,
                                                    const std::vector<LinkFormatStream>& streams,
                                                    const std::string& quality,
                                                    int mediaMode);
std::string StripFormatItemLabel(std::string label);
bool IsInactiveFormatItem(const std::string& label);
int FirstActiveFormatIndex(const std::vector<std::string>& items);

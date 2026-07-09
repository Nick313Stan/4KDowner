#pragma once

#include "DownloadOptions.h"

#include <string>
#include <vector>

struct LinkFormatStream {
    std::string formatId;
    std::string ext;
    int height = 0;
    std::string vcodec;
    std::string acodec;
};

struct PredictedDownload {
    std::string container;
    std::string videoCodec;
    std::string audioCodec;
    std::string resolution;
};

std::vector<LinkFormatStream> ParseLinkFormatStreams(
    const std::string& commaSeparatedFormatIds,
    const std::string& commaSeparatedExts,
    const std::string& commaSeparatedHeights,
    const std::string& commaSeparatedVideoCodecs,
    const std::string& commaSeparatedAudioCodecs);

PredictedDownload PredictDownload(
    const std::vector<LinkFormatStream>& streams,
    const std::vector<std::string>& videoFormats,
    const std::vector<std::string>& audioFormats,
    const std::vector<std::string>& qualities,
    const DownloadOptions& options);

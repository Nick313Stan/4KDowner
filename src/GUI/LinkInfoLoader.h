#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "DownloadFormatPredictor.h"

struct LinkInfo
{
    bool success = false;
    bool cancelled = false;
    std::string url;
    std::string title;
    std::string normalizedTitle;
    std::string uploader;
    std::string duration;
    std::string container;
    std::string videoCodec;
    std::string audioCodec;
    std::string thumbnailPath;
    std::string error;
    std::string errorLog;
    std::string parseBrowserReport;
    std::vector<std::string> availableFormats;
    std::vector<std::string> availableVideoFormats;
    std::vector<std::string> availableAudioFormats;
    std::vector<std::string> availableQualities;
    std::vector<LinkFormatStream> formatStreams;
};

class LinkInfoLoader
{
public:
    LinkInfoLoader() = default;
    ~LinkInfoLoader();
    LinkInfoLoader(const LinkInfoLoader&) = delete;
    LinkInfoLoader& operator=(const LinkInfoLoader&) = delete;
    LinkInfoLoader(LinkInfoLoader&& other) noexcept;
    LinkInfoLoader& operator=(LinkInfoLoader&& other) noexcept;

    void Start(std::string url);
    void Cancel();
    void Update();
    static void ReapAbandoned();

    bool IsLoading() const;
    bool HasResult() const;
    const LinkInfo& GetResult() const;

    static LinkInfo LoadVideo(std::string url, std::shared_ptr<std::atomic_bool> cancelRequested);
    static std::string Quote(const std::string& value);

private:
    static LinkInfo Load(std::string url, std::shared_ptr<std::atomic_bool> cancelRequested);
    void AbandonRunningWork();

    std::future<LinkInfo> future_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    LinkInfo result_;
    bool isLoading_ = false;
    bool hasResult_ = false;
};

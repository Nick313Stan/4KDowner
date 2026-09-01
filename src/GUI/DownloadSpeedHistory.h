#pragma once

#include "DownloadRunner.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class DownloadSpeedHistory
{
public:
    void BeginSession(const DownloadRequest& request);
    void Update(const DownloadRunner* runners, size_t count);

    bool IsRecording() const;
    bool HasSamples() const;
    std::string BuildClipboardText() const;

private:
    struct Sample
    {
        double tSeconds = 0.0;
        double netBps = 0.0;
        double diskBps = 0.0;
        float ytProgress = 0.0f;
        float diskProgress = -1.0f;
        DownloadSharedState::Phase phase = DownloadSharedState::Phase::Downloading;
    };

    void AppendSample(const DownloadRunner& runner);

    std::string url_;
    std::string title_;
    std::string quality_;
    std::string mediaMode_;
    std::string fileFormat_;
    std::int64_t estimatedBytes_ = 0;
    bool recording_ = false;
    std::vector<Sample> samples_;
    std::chrono::steady_clock::time_point sessionStartedAt_{};
    std::chrono::steady_clock::time_point lastSampleAt_{};
};

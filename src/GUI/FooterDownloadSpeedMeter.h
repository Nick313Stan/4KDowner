#pragma once

#include "DownloadRunner.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <string>

class FooterDownloadSpeedMeter
{
public:
    FooterDownloadSpeedMeter();

    template <size_t N>
    void Update(const std::array<DownloadRunner, N>& runners, bool anyDownloadRunning)
    {
        Update(runners.data(), runners.size(), anyDownloadRunning);
    }

    void Update(const DownloadRunner* runners, size_t count, bool anyDownloadRunning);
    const std::string& NetValue() const;
    const std::string& DiskValue() const;

private:
    void RebuildValues();
    void SmoothSample(double& smoothed, double raw, double alpha);

    double ytDlpSpeedBps_ = 0.0;
    double diskSpeedBps_ = 0.0;
    std::string netValue_ = "0.0";
    std::string diskValue_ = "0.0";
    std::chrono::steady_clock::time_point lastLabelRefreshAt_{};
};

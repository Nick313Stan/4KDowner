#include "FooterDownloadSpeedMeter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double kLabelRefreshIntervalSeconds = 0.5;
constexpr double kSmoothTauSeconds = 1.5;
constexpr double kMegabyte = 1000.0 * 1000.0;
constexpr double kMegabit = 1000.0 * 1000.0; // SI megabit (for Mbit/s display)
} // namespace

FooterDownloadSpeedMeter::FooterDownloadSpeedMeter()
{
    RebuildValues();
}

void FooterDownloadSpeedMeter::Update(const DownloadRunner* runners, size_t count, bool anyDownloadRunning)
{
    const auto now = std::chrono::steady_clock::now();
    if (lastLabelRefreshAt_.time_since_epoch().count() != 0)
    {
        const double elapsedSeconds = std::chrono::duration<double>(now - lastLabelRefreshAt_).count();
        if (elapsedSeconds < kLabelRefreshIntervalSeconds)
        {
            return;
        }
    }
    lastLabelRefreshAt_ = now;

    if (!anyDownloadRunning || runners == nullptr || count == 0)
    {
        ytDlpSpeedBps_ = 0.0;
        diskSpeedBps_ = 0.0;
        RebuildValues();
        return;
    }

    double sumYtDlpSpeedBps = 0.0;
    double sumDiskSpeedBps = 0.0;
    bool hasYtDlpSpeed = false;
    bool hasDiskSpeed = false;
    for (size_t index = 0; index < count; ++index)
    {
        const DownloadRunner& runner = runners[index];
        if (!runner.IsRunning())
        {
            continue;
        }

        const double ytDlpSpeed = runner.YtDlpSpeedBps();
        if (ytDlpSpeed >= 0.0)
        {
            sumYtDlpSpeedBps += ytDlpSpeed;
            hasYtDlpSpeed = true;
        }

        const double diskSpeed = runner.DiskSpeedBps();
        if (diskSpeed >= 0.0)
        {
            sumDiskSpeedBps += diskSpeed;
            hasDiskSpeed = true;
        }
    }

    const double alpha = 1.0 - std::exp(-kLabelRefreshIntervalSeconds / kSmoothTauSeconds);

    if (hasYtDlpSpeed)
    {
        SmoothSample(ytDlpSpeedBps_, sumYtDlpSpeedBps, alpha);
    }
    else
    {
        ytDlpSpeedBps_ = 0.0;
    }

    if (hasDiskSpeed)
    {
        SmoothSample(diskSpeedBps_, sumDiskSpeedBps, alpha);
    }
    else
    {
        diskSpeedBps_ = 0.0;
    }

    RebuildValues();
}

const std::string& FooterDownloadSpeedMeter::NetValue() const
{
    return netValue_;
}

const std::string& FooterDownloadSpeedMeter::DiskValue() const
{
    return diskValue_;
}

void FooterDownloadSpeedMeter::RebuildValues()
{
    char buffer[32];
    // net: bits/s → Mbit/s (matches Task Manager); disk stays MB/s (bytes).
    std::snprintf(buffer, sizeof(buffer), "%.1f", (ytDlpSpeedBps_ * 8.0) / kMegabit);
    netValue_ = buffer;

    std::snprintf(buffer, sizeof(buffer), "%.1f", diskSpeedBps_ / kMegabyte);
    diskValue_ = buffer;
}

void FooterDownloadSpeedMeter::SmoothSample(double& smoothed, double raw, double alpha)
{
    alpha = std::clamp(alpha, 0.0, 1.0);
    smoothed += alpha * (raw - smoothed);
}

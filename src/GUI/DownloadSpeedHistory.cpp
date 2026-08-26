#include "DownloadSpeedHistory.h"

#include "Version.h"

#include <cstdio>
#include <sstream>

namespace
{
constexpr double kSampleIntervalSeconds = 0.5;
constexpr size_t kMaxSamples = 2400; // ~20 min at 0.5s

const char* PhaseName(DownloadSharedState::Phase phase)
{
    return phase == DownloadSharedState::Phase::Merging ? "Merging" : "Downloading";
}

std::string DetectOsLabel()
{
#ifdef _WIN32
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown";
#endif
}
} // namespace

void DownloadSpeedHistory::BeginSession(const DownloadRequest& request)
{
    url_ = request.url;
    title_ = request.title;
    quality_ = request.quality;
    mediaMode_ = request.mediaMode;
    fileFormat_ = request.fileFormat;
    estimatedBytes_ = request.estimatedBytes;
    samples_.clear();
    samples_.reserve(256);
    recording_ = true;
    sessionStartedAt_ = std::chrono::steady_clock::now();
    lastSampleAt_ = {};
}

void DownloadSpeedHistory::Update(const DownloadRunner* runners, size_t count)
{
    if (!recording_ || runners == nullptr || count == 0 || url_.empty())
    {
        return;
    }

    const DownloadRunner* tracked = nullptr;
    for (size_t index = 0; index < count; ++index)
    {
        if (runners[index].CurrentUrl() == url_)
        {
            tracked = &runners[index];
            break;
        }
    }

    if (tracked == nullptr)
    {
        recording_ = false;
        return;
    }

    if (!tracked->IsRunning())
    {
        // Capture a final sample if we never got one, then freeze the log.
        if (samples_.empty())
        {
            AppendSample(*tracked);
        }
        recording_ = false;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (lastSampleAt_.time_since_epoch().count() != 0)
    {
        const double elapsed = std::chrono::duration<double>(now - lastSampleAt_).count();
        if (elapsed < kSampleIntervalSeconds)
        {
            return;
        }
    }

    AppendSample(*tracked);
    lastSampleAt_ = now;
}

bool DownloadSpeedHistory::IsRecording() const
{
    return recording_;
}

bool DownloadSpeedHistory::HasSamples() const
{
    return !samples_.empty();
}

std::string DownloadSpeedHistory::BuildClipboardText() const
{
    std::ostringstream out;
    out << "# 4KDowner speed log\n";
    out << "# version=" << FOURKDOWNER_VERSION << "\n";
    out << "# os=" << DetectOsLabel() << "\n";
    out << "# url=" << url_ << "\n";
    out << "# title=" << title_ << "\n";
    out << "# quality=" << quality_ << "\n";
    out << "# media=" << mediaMode_ << "\n";
    out << "# format=" << fileFormat_ << "\n";
    out << "# estimated_bytes=" << estimatedBytes_ << "\n";
    out << "# sample_interval_s=" << kSampleIntervalSeconds << "\n";
    out << "# samples=" << samples_.size() << "\n";
    out << "t_s\tnet_Bps\tdisk_Bps\tyt_pct\tdisk_pct\tphase\n";

    char line[192];
    for (const Sample& sample : samples_)
    {
        const double ytPct = static_cast<double>(sample.ytProgress) * 100.0;
        const double diskPct = sample.diskProgress < 0.0f ? -1.0 : static_cast<double>(sample.diskProgress) * 100.0;
        std::snprintf(line,
                      sizeof(line),
                      "%.1f\t%.0f\t%.0f\t%.1f\t%.1f\t%s\n",
                      sample.tSeconds,
                      sample.netBps,
                      sample.diskBps,
                      ytPct,
                      diskPct,
                      PhaseName(sample.phase));
        out << line;
    }
    return out.str();
}

void DownloadSpeedHistory::AppendSample(const DownloadRunner& runner)
{
    if (samples_.size() >= kMaxSamples)
    {
        return;
    }

    Sample sample;
    sample.tSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - sessionStartedAt_).count();
    const double net = runner.YtDlpSpeedBps();
    const double disk = runner.DiskSpeedBps();
    sample.netBps = net >= 0.0 ? net : 0.0;
    sample.diskBps = disk >= 0.0 ? disk : 0.0;
    sample.ytProgress = runner.Progress();
    sample.diskProgress = runner.DiskProgress();
    sample.phase = runner.Phase();
    samples_.push_back(sample);
}

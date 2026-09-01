#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>

struct DownloadRequest
{
    std::string url;
    // Owning LinkCardNode::InstanceId(); 0 = unknown / legacy match by URL+output.
    std::uint64_t cardInstanceId = 0;
    std::string title;
    std::string normalizedTitle;
    std::string outputDirectory;
    std::string fileFormat;
    std::string mediaMode;
    std::string quality;
    // Upper bound for yt-dlp format selector (height<=N). Empty = use quality or Max.
    std::string qualityCap;
    std::string finalOutputDirectory;
    std::string originalNormalizedTitle;
    std::int64_t estimatedBytes = 0;
    // Disk % from one Title.ext.part (HLS/muxed) vs sum of Title.fNNN.* (DASH Both).
    bool singleMainPartDiskProgress = true;
    bool autoConvertActive = false;
    bool overwriteExisting = false;
    // YouTube live: yt-dlp --live-from-start (begin at stream start, then follow live edge).
    bool liveFromStart = false;
    // Unix seconds when the live started; used for catch-up progress (0 = unknown).
    std::int64_t liveStartUnix = 0;
    // Rough total bitrate for live catch-up estimate (bytes → seconds).
    double estimatedBitrateBps = 0.0;
};

struct DownloadSharedState
{
    mutable std::mutex mutex;
    std::string status;
    float progress = 0.0f;
    // yt-dlp tracker % (Downloading phase only); kept separate from merged progress.
    float ytProgress = 0.0f;
    // Bytes-on-disk vs estimatedBytes; <0 means unknown / do not draw.
    float diskProgress = -1.0f;
    // Total size for disk% (from request and/or yt-dlp "of XGiB" lines).
    std::int64_t estimatedBytes = 0;
    // Latest yt-dlp-reported download speed; <0 means unknown.
    double ytDlpSpeedBps = -1.0;
    std::uint64_t diskBytes = 0;
    enum class Phase
    {
        Downloading,
        Merging,
    };
    Phase phase = Phase::Downloading;
};

struct DownloadRunResult
{
    std::string status;
    std::string errorLog;
    std::string downloadBrowserReport;
    // Original-language title resolved for the output stem (may differ from flat-playlist title).
    std::string resolvedTitle;
    // Final on-disk stem used for -o (includes numbering / _downloaded when applicable).
    std::string resolvedNormalizedTitle;
    int exitCode = 0;
};

// Header-only probe for Both validation / incomplete leftover detection.
enum class MediaAudioProbeResult
{
    Unavailable, // ffprobe missing or could not read the file
    HasAudio,
    MissingAudio,
};

MediaAudioProbeResult ProbeMediaFileAudio(const std::filesystem::path& mediaPath);

class DownloadRunner
{
public:
    void Start(DownloadRequest request);
    void Cancel();
    // Cancel if running, then block until the worker finishes (artifact cleanup runs there).
    void Shutdown();
    void Update();

    bool IsRunning() const;
    const std::string& Status() const;
    const std::string& CurrentUrl() const;
    std::uint64_t CardInstanceId() const;
    // URL + folder + stem + extension — same file on disk (quality does not count).
    const std::string& OutputIdentity() const;
    static std::string MakeOutputIdentity(const DownloadRequest& request);
    float Progress() const;
    // yt-dlp % only (<0 when unknown); use for card green bar during download.
    float YtProgress() const;
    // <0 when estimate/disk size unavailable.
    float DiskProgress() const;
    bool LiveFromStart() const;
    DownloadSharedState::Phase Phase() const;
    double ElapsedSeconds() const;
    // <0 when unavailable.
    double YtDlpSpeedBps() const;
    double DiskSpeedBps() const;
    std::int64_t EstimatedBytes() const;
    std::uint64_t DiskBytes() const;
    bool ConsumeCompletedDownload(std::string& url, double& elapsedSeconds);
    void SetStatus(std::string status);
    const std::string& LastErrorLog() const;
    const std::string& LastDownloadBrowserReport() const;
    const std::string& LastResolvedTitle() const;
    const std::string& LastResolvedNormalizedTitle() const;
    static void SetSharedStatus(const std::shared_ptr<DownloadSharedState>& sharedState,
                                const std::string& status,
                                float progress,
                                DownloadSharedState::Phase phase = DownloadSharedState::Phase::Downloading);
    static void SetSharedDiskProgress(const std::shared_ptr<DownloadSharedState>& sharedState, float diskProgress);
    static void SetSharedDiskBytes(const std::shared_ptr<DownloadSharedState>& sharedState, std::uint64_t diskBytes);
    static void SetSharedEstimatedBytes(const std::shared_ptr<DownloadSharedState>& sharedState,
                                        std::int64_t estimatedBytes);
    static void SetSharedYtDlpSpeed(const std::shared_ptr<DownloadSharedState>& sharedState, double ytDlpSpeedBps);

private:
    static DownloadRunResult Run(DownloadRequest request,
                                 std::shared_ptr<std::atomic_bool> cancelRequested,
                                 std::shared_ptr<DownloadSharedState> sharedState);
    static std::string Quote(const std::string& value);

    std::future<DownloadRunResult> future_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<DownloadSharedState> sharedState_;
    std::string status_;
    std::string lastErrorLog_;
    std::string lastDownloadBrowserReport_;
    std::string lastResolvedTitle_;
    std::string lastResolvedNormalizedTitle_;
    std::string currentUrl_;
    std::uint64_t cardInstanceId_ = 0;
    std::string outputIdentity_;
    std::string completedUrl_;
    std::chrono::steady_clock::time_point startedAt_{};
    double elapsedSeconds_ = 0.0;
    double completedElapsedSeconds_ = 0.0;
    float progress_ = 0.0f;
    float ytProgress_ = -1.0f;
    float diskProgress_ = -1.0f;
    double ytDlpSpeedBps_ = -1.0;
    double diskSpeedBps_ = -1.0;
    std::int64_t estimatedBytes_ = 0;
    std::uint64_t diskBytes_ = 0;
    std::uint64_t lastDiskBytesSample_ = 0;
    std::chrono::steady_clock::time_point lastDiskSampleAt_{};
    DownloadSharedState::Phase phase_ = DownloadSharedState::Phase::Downloading;
    bool hasCompletedDownload_ = false;
    bool isRunning_ = false;
    bool liveFromStart_ = false;
};

#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>

struct DownloadRequest {
    std::string url;
    std::string title;
    std::string normalizedTitle;
    std::string outputDirectory;
    std::string fileFormat;
    std::string mediaMode;
    std::string quality;
    bool overwriteExisting = false;
};

struct DownloadSharedState {
    mutable std::mutex mutex;
    std::string status;
    float progress = 0.0f;
};

struct DownloadRunResult {
    std::string status;
    std::string errorLog;
    std::string downloadBrowserReport;
    int exitCode = 0;
};

class DownloadRunner {
public:
    void Start(DownloadRequest request);
    void Cancel();
    void Update();

    bool IsRunning() const;
    const std::string& Status() const;
    const std::string& CurrentUrl() const;
    float Progress() const;
    double ElapsedSeconds() const;
    bool ConsumeCompletedDownload(std::string& url, double& elapsedSeconds);
    void SetStatus(std::string status);
    const std::string& LastErrorLog() const;
    const std::string& LastDownloadBrowserReport() const;
    static void SetSharedStatus(const std::shared_ptr<DownloadSharedState>& sharedState, const std::string& status, float progress);

private:
    static DownloadRunResult Run(
        DownloadRequest request,
        std::shared_ptr<std::atomic_bool> cancelRequested,
        std::shared_ptr<DownloadSharedState> sharedState);
    static std::string Quote(const std::string& value);

    std::future<DownloadRunResult> future_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<DownloadSharedState> sharedState_;
    std::string status_;
    std::string lastErrorLog_;
    std::string lastDownloadBrowserReport_;
    std::string currentUrl_;
    std::string completedUrl_;
    std::chrono::steady_clock::time_point startedAt_{};
    double elapsedSeconds_ = 0.0;
    double completedElapsedSeconds_ = 0.0;
    float progress_ = 0.0f;
    bool hasCompletedDownload_ = false;
    bool isRunning_ = false;
};

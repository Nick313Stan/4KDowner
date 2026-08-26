#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>

struct ConvertRequest
{
    std::string inputPath;
    std::string outputDirectory;
    // If set, final file is "{outputBaseName}.{ext}" instead of "{stem}_converted.{ext}".
    std::string outputBaseName;
    // Downloader auto-convert: URL of the LinkCardNode that owns this job.
    std::string linkCardUrl;
    std::string container;
    bool convertContainer = false;
    std::string videoCodec;
    bool convertVideo = false;
    std::string audioCodec;
    bool convertAudio = false;
    bool overwriteExisting = false;
    bool deleteInputOnSuccess = false;
    double sourceDurationSeconds = 0.0;
};

struct ConvertSharedState
{
    mutable std::mutex mutex;
    float progress = 0.0f;
};

struct ConvertRunResult
{
    std::string status;
    std::string errorLog;
};

class ConvertRunner
{
public:
    void Start(ConvertRequest request);
    void Cancel();
    // Cancel if running, then block until the worker finishes.
    void Shutdown();
    void Update();
    void SetStatus(std::string status);

    bool IsRunning() const;
    const std::string& Status() const;
    const std::string& LastErrorLog() const;
    const std::string& CurrentInputPath() const;
    float Progress() const;
    double ElapsedSeconds() const;
    double SourceDurationSeconds() const;
    bool ConsumeCompletedConvert(std::string& inputPath,
                                 std::string& outputPath,
                                 double& elapsedSeconds,
                                 std::string& linkCardUrl,
                                 bool& deleteInputOnSuccess);

    static std::filesystem::path GetOutputPath(const ConvertRequest& request);

private:
    static ConvertRunResult Run(ConvertRequest request,
                                std::shared_ptr<std::atomic_bool> cancelRequested,
                                std::shared_ptr<ConvertSharedState> sharedState);
    static std::string Quote(const std::string& value);

    std::future<ConvertRunResult> future_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<ConvertSharedState> sharedState_;
    std::string status_;
    std::string lastErrorLog_;
    std::string currentInputPath_;
    ConvertRequest activeRequest_{};
    float progress_ = 0.0f;
    double elapsedSeconds_ = 0.0;
    double completedElapsedSeconds_ = 0.0;
    std::string completedInputPath_;
    std::string completedOutputPath_;
    std::string completedLinkCardUrl_;
    bool completedDeleteInputOnSuccess_ = false;
    bool hasCompletedConvert_ = false;
    double sourceDurationSeconds_ = 0.0;
    std::chrono::steady_clock::time_point startedAt_{};
    bool isRunning_ = false;
};

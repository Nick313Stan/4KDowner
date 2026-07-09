#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>

struct ConvertRequest {
    std::string inputPath;
    std::string outputDirectory;
    std::string container;
    bool convertContainer = false;
    std::string videoCodec;
    bool convertVideo = false;
    std::string audioCodec;
    bool convertAudio = false;
    bool overwriteExisting = false;
    double sourceDurationSeconds = 0.0;
};

struct ConvertSharedState {
    mutable std::mutex mutex;
    float progress = 0.0f;
};

struct ConvertRunResult {
    std::string status;
    std::string errorLog;
};

class ConvertRunner {
public:
    void Start(ConvertRequest request);
    void Cancel();
    void Update();
    void SetStatus(std::string status);

    bool IsRunning() const;
    const std::string& Status() const;
    const std::string& LastErrorLog() const;
    const std::string& CurrentInputPath() const;
    float Progress() const;
    double ElapsedSeconds() const;
    bool ConsumeCompletedConvert(std::string& inputPath, double& elapsedSeconds);

    static std::filesystem::path GetOutputPath(const ConvertRequest& request);

private:
    static ConvertRunResult Run(
        ConvertRequest request,
        std::shared_ptr<std::atomic_bool> cancelRequested,
        std::shared_ptr<ConvertSharedState> sharedState);
    static std::string Quote(const std::string& value);

    std::future<ConvertRunResult> future_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<ConvertSharedState> sharedState_;
    std::string status_;
    std::string lastErrorLog_;
    std::string currentInputPath_;
    float progress_ = 0.0f;
    double elapsedSeconds_ = 0.0;
    double completedElapsedSeconds_ = 0.0;
    std::string completedInputPath_;
    bool hasCompletedConvert_ = false;
    double sourceDurationSeconds_ = 0.0;
    std::chrono::steady_clock::time_point startedAt_{};
    bool isRunning_ = false;
};

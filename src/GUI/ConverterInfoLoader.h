#pragma once

#include <future>
#include <string>

struct ConverterFileInfo
{
    bool success = false;
    std::string filePath;
    std::string fileName;
    std::string duration;
    double durationSeconds = 0.0;
    std::string container;
    std::string videoCodec;
    std::string audioCodec;
    int width = 0;
    int height = 0;
    std::string resolution;
    std::string previewPath;
    std::string error;
};

class ConverterInfoLoader
{
public:
    ConverterInfoLoader() = default;
    ~ConverterInfoLoader();
    ConverterInfoLoader(const ConverterInfoLoader&) = delete;
    ConverterInfoLoader& operator=(const ConverterInfoLoader&) = delete;
    ConverterInfoLoader(ConverterInfoLoader&& other) noexcept;
    ConverterInfoLoader& operator=(ConverterInfoLoader&& other) noexcept;

    void Start(std::string filePath);
    void Cancel();
    void Update();
    void ClearResult();
    static void ReapAbandoned();

    bool IsLoading() const;
    bool HasResult() const;
    const ConverterFileInfo& GetResult() const;

private:
    static ConverterFileInfo Load(std::string filePath);
    static std::string Quote(const std::string& value);
    void AbandonRunningWork();

    std::future<ConverterFileInfo> future_;
    ConverterFileInfo result_;
    bool isLoading_ = false;
    bool hasResult_ = false;
};

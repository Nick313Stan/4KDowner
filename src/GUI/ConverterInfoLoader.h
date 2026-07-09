#pragma once

#include <future>
#include <string>

struct ConverterFileInfo {
    bool success = false;
    std::string filePath;
    std::string fileName;
    std::string duration;
    double durationSeconds = 0.0;
    std::string container;
    std::string videoCodec;
    std::string audioCodec;
    std::string previewPath;
    std::string error;
};

class ConverterInfoLoader {
public:
    void Start(std::string filePath);
    void Cancel();
    void Update();
    void ClearResult();

    bool IsLoading() const;
    bool HasResult() const;
    const ConverterFileInfo& GetResult() const;

private:
    static ConverterFileInfo Load(std::string filePath);
    static std::string Quote(const std::string& value);

    std::future<ConverterFileInfo> future_;
    ConverterFileInfo result_;
    bool isLoading_ = false;
    bool hasResult_ = false;
};

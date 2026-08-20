#pragma once

#include "ConverterInfoLoader.h"
#include "DownloadOptions.h"
#include "raylib.h"

#include <string>

class ConverterFileCardNode
{
public:
    ConverterFileCardNode() = default;
    ~ConverterFileCardNode();

    ConverterFileCardNode(const ConverterFileCardNode&) = delete;
    ConverterFileCardNode& operator=(const ConverterFileCardNode&) = delete;
    ConverterFileCardNode(ConverterFileCardNode&& other) noexcept;
    ConverterFileCardNode& operator=(ConverterFileCardNode&& other) noexcept;

    void SetInfo(ConverterFileInfo info);
    void StartLoading(std::string filePath);
    void Update(Rectangle bounds, Font font);
    void Draw(Rectangle bounds, Font font, int displayIndex = 0) const;
    void Clear();
    void TriggerPulse();
    void SetSelected(bool selected);
    void SetConverting();
    void ClearConverting();
    void SetConvertingElapsed(double seconds);
    void SetConvertElapsed(double seconds);
    void SetLastConvertedPath(std::string path);
    void ClearLastConvertedPath();
    void SetOperationProgress(float progress);
    void ClearOperationProgress();
    void SetLastError(std::string error);
    void ClearLastError();
    bool IsConverting() const;
    bool HasCompletedConvert() const;
    double ConvertElapsedSeconds() const;
    const std::string& LastConvertedPath() const;

    bool HasFile() const;
    bool IsLoading() const;
    bool HasFilePath(const std::string& filePath) const;
    bool ShouldClose() const;
    void RequestClose();
    bool IsHovered() const;
    bool WasConvertCancelClicked() const;
    bool WasClicked() const;
    bool WasCopyClicked() const;
    bool WasOpenPathClicked() const;
    bool IsSelected() const;
    bool WasDismissedDuringLoad() const;
    bool TryConsumeLoadSuccess();
    bool TryConsumeLoadFailure(std::string& error);
    void CancelLoading();
    bool CanCopyInfo() const;
    bool CanRevealPath() const;
    std::string BuildCopyPayload() const;
    std::string ResolvePathForReveal() const;
    const ConverterFileInfo& Info() const;
    bool UseDefaultConvertSettings() const;
    void SetUseDefaultConvertSettings(bool useDefault);
    ConverterOptions& CustomConvertOptions();
    const ConverterOptions& CustomConvertOptions() const;

private:
    void DrawMiniSpinner(Vector2 center) const;
    void DrawBackgroundProgress(Rectangle bounds, float roundness) const;
    void LoadPreview();
    void UnloadPreview();

    ConverterFileInfo info_;
    ConverterInfoLoader loader_;
    Texture2D previewTexture_{};
    bool hasPreviewTexture_ = false;
    bool triedLoadingPreview_ = false;
    bool hasFile_ = false;
    bool isLoading_ = false;
    bool dismissedDuringLoad_ = false;
    bool isHovered_ = false;
    bool isSelected_ = false;
    bool wasClicked_ = false;
    bool shouldClose_ = false;
    bool wasConvertCancelClicked_ = false;
    bool wasCopyClicked_ = false;
    bool wasOpenPathClicked_ = false;
    bool pendingLoadSuccessReport_ = false;
    bool pendingLoadErrorReport_ = false;
    mutable bool hasConvertStatusBounds_ = false;
    mutable Rectangle convertStatusBounds_{};
    bool isConverting_ = false;
    bool hasConvertElapsed_ = false;
    double convertingElapsedSeconds_ = 0.0;
    double convertElapsedSeconds_ = 0.0;
    float operationProgress_ = -1.0f;
    double pulseStartTime_ = -10.0;
    std::string lastErrorText_;
    std::string lastConvertedPath_;
    bool useDefaultConvertSettings_ = true;
    ConverterOptions customOptions_;

    static constexpr double kPulseSeconds = 0.34;
};

#pragma once

#include "ConverterInfoLoader.h"
#include "raylib.h"

class ConverterFileCardNode {
public:
    ConverterFileCardNode() = default;
    ~ConverterFileCardNode();

    ConverterFileCardNode(const ConverterFileCardNode&) = delete;
    ConverterFileCardNode& operator=(const ConverterFileCardNode&) = delete;
    ConverterFileCardNode(ConverterFileCardNode&& other) noexcept;
    ConverterFileCardNode& operator=(ConverterFileCardNode&& other) noexcept;

    void SetInfo(ConverterFileInfo info);
    void StartLoading(std::string filePath);
    void Update(Rectangle bounds);
    void Draw(Rectangle bounds, Font font) const;
    void Clear();
    void TriggerPulse();
    void SetSelected(bool selected);
    void SetConverting();
    void ClearConverting();
    void SetConvertingElapsed(double seconds);
    void SetConvertElapsed(double seconds);
    void SetOperationProgress(float progress);
    void ClearOperationProgress();
    bool IsConverting() const;
    bool HasCompletedConvert() const;
    double ConvertElapsedSeconds() const;

    bool HasFile() const;
    bool IsLoading() const;
    bool HasFilePath(const std::string& filePath) const;
    bool ShouldClose() const;
    bool WasConvertCancelClicked() const;
    bool WasClicked() const;
    bool IsSelected() const;
    bool TryConsumeLoadSuccess();
    bool TryConsumeLoadFailure(std::string& error);
    void CancelLoading();
    const ConverterFileInfo& Info() const;

private:
    Rectangle GetAnimatedBounds(Rectangle bounds) const;
    Rectangle GetCloseButtonBounds(Rectangle bounds) const;
    void DrawCloseButton(Rectangle bounds) const;
    void DrawMiniSpinner(Vector2 center) const;
    void DrawBackgroundProgress(Rectangle bounds, float roundness) const;
    void DrawWrappedText(Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color) const;
    void LoadPreview();
    void UnloadPreview();

    ConverterFileInfo info_;
    ConverterInfoLoader loader_;
    Texture2D previewTexture_{};
    bool hasPreviewTexture_ = false;
    bool triedLoadingPreview_ = false;
    bool hasFile_ = false;
    bool isLoading_ = false;
    bool isHovered_ = false;
    bool isSelected_ = false;
    bool wasClicked_ = false;
    bool shouldClose_ = false;
    bool wasConvertCancelClicked_ = false;
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

    static constexpr double kPulseSeconds = 0.34;
};

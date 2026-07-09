#pragma once

#include "DownloadOptions.h"
#include "LinkInfoLoader.h"
#include "raylib.h"

enum class CardQueueStatus {
    None,
    Downloading,
    InQueue,
    NotInQueue,
    Cancelled,
};

class LinkCardNode {
public:
    explicit LinkCardNode(LinkInfo info);
    explicit LinkCardNode(std::string url);
    ~LinkCardNode();

    LinkCardNode(const LinkCardNode&) = delete;
    LinkCardNode& operator=(const LinkCardNode&) = delete;
    LinkCardNode(LinkCardNode&& other) noexcept;
    LinkCardNode& operator=(LinkCardNode&& other) noexcept;

    void Update(Rectangle bounds);
    void Draw(Rectangle bounds, Font font) const;
    void TriggerPulse();
    void SetSelected(bool selected);
    void SetDownloading();
    void SetQueued();
    void SetNotInQueue();
    void ClearQueueState();
    void SetDownloadElapsed(double seconds);
    void ClearDownloading();
    void SetOperationProgress(float progress);
    void ClearOperationProgress();
    bool ShouldClose() const;
    bool WasClicked() const;
    bool WasDownloadCancelClicked() const;
    bool WasRedownloadClicked() const;
    bool WasQueueDownloadClicked() const;
    bool WasQueueCancelClicked() const;
    bool WasCopyClicked() const;
    bool HasUrl(const std::string& url) const;
    bool IsDownloading() const;
    bool IsCancelled() const;
    bool IsInQueue() const;
    bool IsNotInQueue() const;
    bool HasCompletedDownload() const;
    double DownloadElapsedSeconds() const;
    bool IsSelected() const;
    bool IsValid() const;
    bool IsParsing() const;
    bool TryConsumeParseFailure(std::string& url, std::string& error);
    bool TryConsumeParseSuccess(std::string& url);
    const std::string& ParseBrowserReport() const;
    void SetDownloadBrowserReport(const std::string& report);
    std::string BuildBrowserDiagnosticsReport() const;
    bool HasBrowserDiagnostics() const;
    const std::string& Url() const;
    const std::string& Title() const;
    const std::string& NormalizedTitle() const;
    const std::vector<std::string>& AvailableFormats() const;
    const std::vector<std::string>& AvailableVideoFormats() const;
    const std::vector<std::string>& AvailableAudioFormats() const;
    const std::vector<std::string>& AvailableQualities() const;
    const std::vector<LinkFormatStream>& FormatStreams() const;
    DownloadOptions& Options();
    const DownloadOptions& Options() const;

private:
    Rectangle GetAnimatedBounds(Rectangle bounds) const;
    Rectangle GetCloseButtonBounds(Rectangle bounds) const;
    Rectangle GetCopyButtonBounds(Rectangle bounds) const;
    void DrawCopyButton(Rectangle bounds) const;
    void LoadThumbnail();
    void UnloadThumbnail();
    void DrawWrappedText(Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color) const;
    void DrawCloseButton(Rectangle bounds) const;
    Rectangle GetDownloadStatusBounds(Rectangle bounds, Font font, float x, const std::string& label) const;
    void DrawMiniSpinner(Vector2 center) const;
    void DrawBackgroundProgress(Rectangle bounds, float roundness) const;
    void ApplyParseResultIfReady();

    LinkInfo info_;
    LinkInfoLoader loader_;
    bool isParsing_ = false;
    Texture2D thumbnailTexture_{};
    bool hasThumbnailTexture_ = false;
    bool triedLoadingThumbnail_ = false;
    bool isHovered_ = false;
    bool isSelected_ = false;
    bool wasClicked_ = false;
    bool wasDownloadCancelClicked_ = false;
    bool wasRedownloadClicked_ = false;
    bool wasQueueDownloadClicked_ = false;
    bool wasQueueCancelClicked_ = false;
    bool wasCopyClicked_ = false;
    bool shouldClose_ = false;
    bool pendingParseErrorReport_ = false;
    bool pendingParseSuccessReport_ = false;
    std::string downloadBrowserReport_;
    DownloadOptions options_;
    double downloadElapsedSeconds_ = 0.0;
    bool hasDownloadElapsed_ = false;
    CardQueueStatus queueStatus_ = CardQueueStatus::None;
    mutable Rectangle downloadStatusBounds_{};
    mutable bool hasDownloadStatusBounds_ = false;
    double pulseStartTime_ = -10.0;
    float operationProgress_ = -1.0f;

    static constexpr double kPulseSeconds = 0.34;
};

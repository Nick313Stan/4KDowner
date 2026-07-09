#pragma once

#include "DownloadOptions.h"
#include "LinkInfoLoader.h"
#include "raylib.h"

enum class CardQueueStatus
{
    None,
    Downloading,
    InQueue,
    NotInQueue,
    Cancelled,
};

class LinkCardNode
{
public:
    explicit LinkCardNode(LinkInfo info);
    explicit LinkCardNode(std::string url);
    ~LinkCardNode();

    LinkCardNode(const LinkCardNode&) = delete;
    LinkCardNode& operator=(const LinkCardNode&) = delete;
    LinkCardNode(LinkCardNode&& other) noexcept;
    LinkCardNode& operator=(LinkCardNode&& other) noexcept;

    void Update(Rectangle bounds, Font font);
    void Draw(Rectangle bounds,
              Font font,
              bool highlightExcluded = false,
              bool highlightCustom = false,
              int displayIndex = 0) const;
    void TriggerPulse();
    void SetSelected(bool selected);
    void SetDownloading();
    void SetQueued();
    void SetNotInQueue();
    void ClearQueueState();
    void SetDownloadElapsed(double seconds);
    void ClearDownloading();
    void DemoteDownloadingToQueued();
    void SetConverting();
    void ClearConverting();
    void SetBusyStatusLabel(std::string label);
    const std::string& BusyStatusLabel() const;
    void SetConvertElapsed(double seconds);
    void SetExpectedDownloadOutput(std::string directory, std::string fileFormat, std::string normalizedTitle);
    void SetAutoConvertDelivery(std::string finalDirectory, std::string originalNormalizedTitle);
    void ClearAutoConvertDelivery();
    void SetAutoConvertSnapshot(AutoConvertOptions options);
    void ClearAutoConvertSnapshot();
    bool HasAutoConvertSnapshot() const;
    const AutoConvertOptions& AutoConvertSnapshot() const;
    void SetLastDownloadedPath(std::string path);
    void SetOperationProgress(float progress);
    void ClearOperationProgress();
    bool ShouldClose() const;
    void RequestClose();
    bool IsHovered() const;
    bool WasClicked() const;
    bool WasDownloadCancelClicked() const;
    bool WasConvertCancelClicked() const;
    bool WasRedownloadClicked() const;
    bool WasQueueDownloadClicked() const;
    bool WasPrioritizeClicked() const;
    bool WasCopyClicked() const;
    bool WasOpenPathClicked() const;
    bool WasSourceClicked() const;
    std::string ResolveOutputPathForReveal() const;
    bool HasUrl(const std::string& url) const;
    bool HasDownloadedPath(const std::string& path) const;
    bool IsDownloading() const;
    bool IsConverting() const;
    bool IsCancelled() const;
    bool IsInQueue() const;
    bool IsNotInQueue() const;
    bool HasCompletedDownload() const;
    bool HasCompletedConvert() const;
    double DownloadElapsedSeconds() const;
    double ConvertElapsedSeconds() const;
    bool IsSelected() const;
    bool IsValid() const;
    bool IsParsing() const;
    bool WasDismissedDuringParse() const;
    bool TryConsumeParseFailure(std::string& url, std::string& error);
    bool TryConsumeParseSuccess(std::string& url);
    const std::string& ParseBrowserReport() const;
    void SetDownloadBrowserReport(const std::string& report);
    std::string BuildBrowserDiagnosticsReport() const;
    bool HasBrowserDiagnostics() const;
    const std::string& Url() const;
    const std::string& Title() const;
    const std::string& NormalizedTitle() const;
    const std::string& LastDownloadedPath() const;
    const std::string& ExpectedOutputDirectory() const;
    const std::string& ExpectedFileFormat() const;
    const std::string& ExpectedNormalizedTitle() const;
    const std::string& FinalOutputDirectory() const;
    const std::string& OriginalNormalizedTitle() const;
    bool HasAutoConvertDelivery() const;
    void SetAutoConvertStagingPath(std::string path);
    const std::string& AutoConvertStagingPath() const;
    bool IsExcludedFromAutoConvert() const;
    void SetExcludedFromAutoConvert(bool excluded);
    const AutoConvertOptions& CustomAutoConvert() const;
    void SetCustomAutoConvert(AutoConvertOptions options);
    double DurationSeconds() const;
    const std::vector<std::string>& AvailableFormats() const;
    const std::vector<std::string>& AvailableVideoFormats() const;
    const std::vector<std::string>& AvailableAudioFormats() const;
    const std::vector<std::string>& AvailableQualities() const;
    const std::vector<LinkFormatStream>& FormatStreams() const;
    DownloadOptions& Options();
    const DownloadOptions& Options() const;
    const LinkInfo& Info() const;

    void EnsureDetailedParse();
    bool NeedsDetailedParse() const;

private:
    bool CanRevealOutputPath() const;
    void LoadThumbnail();
    void UnloadThumbnail();
    Rectangle GetDownloadStatusBounds(Rectangle bounds, Font font, float x, const std::string& label) const;
    void DrawMiniSpinner(Vector2 center) const;
    void DrawBackgroundProgress(Rectangle bounds, float roundness) const;
    void ApplyParseResultIfReady();

    bool needsDetailedParse_ = false;
    bool isDetailParsing_ = false;
    LinkInfo info_;
    LinkInfoLoader loader_;
    bool isParsing_ = false;
    bool dismissedDuringParse_ = false;
    Texture2D thumbnailTexture_{};
    bool hasThumbnailTexture_ = false;
    bool triedLoadingThumbnail_ = false;
    bool isHovered_ = false;
    bool isSelected_ = false;
    bool wasClicked_ = false;
    bool wasDownloadCancelClicked_ = false;
    bool wasConvertCancelClicked_ = false;
    bool wasRedownloadClicked_ = false;
    bool wasQueueDownloadClicked_ = false;
    bool wasPrioritizeClicked_ = false;
    bool wasCopyClicked_ = false;
    bool wasOpenPathClicked_ = false;
    bool wasSourceClicked_ = false;
    bool shouldClose_ = false;
    bool pendingParseErrorReport_ = false;
    bool pendingParseSuccessReport_ = false;
    std::string downloadBrowserReport_;
    std::string lastDownloadedPath_;
    std::string expectedOutputDirectory_;
    std::string expectedFileFormat_;
    std::string expectedNormalizedTitle_;
    std::string finalOutputDirectory_;
    std::string originalNormalizedTitle_;
    std::string autoConvertStagingPath_;
    bool hasAutoConvertDelivery_ = false;
    bool hasAutoConvertSnapshot_ = false;
    bool excludeFromAutoConvert_ = false;
    AutoConvertOptions customAutoConvert_{};
    AutoConvertOptions autoConvertSnapshot_{};
    DownloadOptions options_;
    double downloadElapsedSeconds_ = 0.0;
    double convertElapsedSeconds_ = 0.0;
    bool hasDownloadElapsed_ = false;
    bool hasConvertElapsed_ = false;
    bool isConverting_ = false;
    CardQueueStatus queueStatus_ = CardQueueStatus::None;
    std::string busyStatusLabel_ = "downloading";
    mutable Rectangle downloadStatusBounds_{};
    mutable bool hasDownloadStatusBounds_ = false;
    mutable Rectangle sourceBounds_{};
    mutable bool hasSourceBounds_ = false;
    double pulseStartTime_ = -10.0;
    float operationProgress_ = -1.0f;
    // GetTime() when download/convert session began; kept across download→auto-convert.
    double busySessionStart_ = -1.0;

    static constexpr double kPulseSeconds = 0.34;
};

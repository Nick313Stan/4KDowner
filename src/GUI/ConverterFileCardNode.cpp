#include "ConverterFileCardNode.h"

#include "CardChrome.h"
#include "MouseCursor.h"
#include "Tooltip.h"
#include "UiClip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <utility>
#include <vector>

namespace
{
std::string FormatElapsed(double seconds)
{
    if (seconds <= 0.0)
    {
        return "0:00";
    }

    const int totalSeconds = static_cast<int>(seconds + 0.5);
    const int hours = totalSeconds / 3600;
    const int remainder = totalSeconds % 3600;
    const int minutes = remainder / 60;
    const int secs = remainder % 60;
    char buffer[32]{};
    if (hours > 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    }
    return buffer;
}

constexpr float kPreviewRoundness = 0.12f;
constexpr int kPreviewSegments = 8;
constexpr float kPreviewWidth = 82.0f;
constexpr float kPreviewHeight = kPreviewWidth * 9.0f / 16.0f;
constexpr int kPreviewPixelWidth = 164;
constexpr int kPreviewPixelHeight = 92;

Rectangle GetPreviewBounds(Rectangle cardBounds)
{
    return {
        cardBounds.x + 8.0f, cardBounds.y + (cardBounds.height - kPreviewHeight) * 0.5f, kPreviewWidth, kPreviewHeight};
}

void PreparePreviewImage(Image& image)
{
    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        return;
    }

    const float targetAspect = 16.0f / 9.0f;
    const float sourceAspect = static_cast<float>(image.width) / static_cast<float>(image.height);

    int cropX = 0;
    int cropY = 0;
    int cropW = image.width;
    int cropH = image.height;
    if (sourceAspect > targetAspect)
    {
        cropW = static_cast<int>(static_cast<float>(image.height) * targetAspect + 0.5f);
        cropW = std::clamp(cropW, 1, image.width);
        cropX = (image.width - cropW) / 2;
    }
    else if (sourceAspect < targetAspect)
    {
        cropH = static_cast<int>(static_cast<float>(image.width) / targetAspect + 0.5f);
        cropH = std::clamp(cropH, 1, image.height);
        cropY = (image.height - cropH) / 2;
    }

    if (cropW != image.width || cropH != image.height)
    {
        ImageCrop(&image,
                  {static_cast<float>(cropX),
                   static_cast<float>(cropY),
                   static_cast<float>(cropW),
                   static_cast<float>(cropH)});
    }

    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        return;
    }

    ImageResize(&image, kPreviewPixelWidth, kPreviewPixelHeight);
}

bool TryDrawMetaSegmentTooltip(
    Font font, const std::string& label, const char* tooltip, float& cursorX, float metaY, float metaHeight)
{
    const float labelWidth = MeasureTextEx(font, label.c_str(), 14.0f, 0.0f).x;
    const Rectangle bounds = {cursorX, metaY - 2.0f, labelWidth, metaHeight};
    cursorX += labelWidth;
    Tooltip::DrawIfHovered(font, bounds, tooltip);
    return CheckCollisionPointRec(GetMousePosition(), bounds);
}
} // namespace

ConverterFileCardNode::~ConverterFileCardNode()
{
    if (isLoading_)
    {
        loader_.Cancel();
    }
    UnloadPreview();
}

ConverterFileCardNode::ConverterFileCardNode(ConverterFileCardNode&& other) noexcept
    : info_(std::move(other.info_)),
      loader_(std::move(other.loader_)),
      previewTexture_(other.previewTexture_),
      hasPreviewTexture_(other.hasPreviewTexture_),
      triedLoadingPreview_(other.triedLoadingPreview_),
      hasFile_(other.hasFile_),
      isLoading_(other.isLoading_),
      dismissedDuringLoad_(other.dismissedDuringLoad_),
      isHovered_(other.isHovered_),
      isSelected_(other.isSelected_),
      wasClicked_(other.wasClicked_),
      shouldClose_(other.shouldClose_),
      wasConvertCancelClicked_(other.wasConvertCancelClicked_),
      wasCopyClicked_(other.wasCopyClicked_),
      wasOpenPathClicked_(other.wasOpenPathClicked_),
      pendingLoadSuccessReport_(other.pendingLoadSuccessReport_),
      pendingLoadErrorReport_(other.pendingLoadErrorReport_),
      isConverting_(other.isConverting_),
      hasConvertElapsed_(other.hasConvertElapsed_),
      convertingElapsedSeconds_(other.convertingElapsedSeconds_),
      convertElapsedSeconds_(other.convertElapsedSeconds_),
      operationProgress_(other.operationProgress_),
      pulseStartTime_(other.pulseStartTime_),
      lastErrorText_(std::move(other.lastErrorText_)),
      lastConvertedPath_(std::move(other.lastConvertedPath_)),
      useDefaultConvertSettings_(other.useDefaultConvertSettings_),
      customOptions_(other.customOptions_)
{
    other.previewTexture_ = {};
    other.hasPreviewTexture_ = false;
    other.hasFile_ = false;
    other.isLoading_ = false;
    other.dismissedDuringLoad_ = false;
    other.pendingLoadSuccessReport_ = false;
    other.pendingLoadErrorReport_ = false;
}

ConverterFileCardNode& ConverterFileCardNode::operator=(ConverterFileCardNode&& other) noexcept
{
    if (this != &other)
    {
        if (isLoading_)
        {
            loader_.Cancel();
        }
        UnloadPreview();
        info_ = std::move(other.info_);
        loader_ = std::move(other.loader_);
        previewTexture_ = other.previewTexture_;
        hasPreviewTexture_ = other.hasPreviewTexture_;
        triedLoadingPreview_ = other.triedLoadingPreview_;
        hasFile_ = other.hasFile_;
        isLoading_ = other.isLoading_;
        dismissedDuringLoad_ = other.dismissedDuringLoad_;
        isHovered_ = other.isHovered_;
        isSelected_ = other.isSelected_;
        wasClicked_ = other.wasClicked_;
        shouldClose_ = other.shouldClose_;
        wasConvertCancelClicked_ = other.wasConvertCancelClicked_;
        wasCopyClicked_ = other.wasCopyClicked_;
        wasOpenPathClicked_ = other.wasOpenPathClicked_;
        pendingLoadSuccessReport_ = other.pendingLoadSuccessReport_;
        pendingLoadErrorReport_ = other.pendingLoadErrorReport_;
        isConverting_ = other.isConverting_;
        hasConvertElapsed_ = other.hasConvertElapsed_;
        convertingElapsedSeconds_ = other.convertingElapsedSeconds_;
        convertElapsedSeconds_ = other.convertElapsedSeconds_;
        operationProgress_ = other.operationProgress_;
        pulseStartTime_ = other.pulseStartTime_;
        lastErrorText_ = std::move(other.lastErrorText_);
        lastConvertedPath_ = std::move(other.lastConvertedPath_);
        useDefaultConvertSettings_ = other.useDefaultConvertSettings_;
        customOptions_ = other.customOptions_;

        other.previewTexture_ = {};
        other.hasPreviewTexture_ = false;
        other.hasFile_ = false;
        other.isLoading_ = false;
        other.dismissedDuringLoad_ = false;
        other.pendingLoadSuccessReport_ = false;
        other.pendingLoadErrorReport_ = false;
    }

    return *this;
}

void ConverterFileCardNode::SetInfo(ConverterFileInfo info)
{
    UnloadPreview();
    info_ = std::move(info);
    hasFile_ = true;
    isLoading_ = false;
    triedLoadingPreview_ = false;
    shouldClose_ = false;
    wasClicked_ = false;
    pendingLoadSuccessReport_ = false;
    pendingLoadErrorReport_ = false;
    if (info_.success)
    {
        ClearLastError();
    }
}

void ConverterFileCardNode::StartLoading(std::string filePath)
{
    if (isLoading_)
    {
        loader_.Cancel();
    }
    UnloadPreview();
    info_ = {};
    info_.filePath = std::move(filePath);
    const size_t separator = info_.filePath.find_last_of("/\\");
    info_.fileName = separator == std::string::npos ? info_.filePath : info_.filePath.substr(separator + 1);
    hasFile_ = true;
    isLoading_ = true;
    dismissedDuringLoad_ = false;
    triedLoadingPreview_ = false;
    shouldClose_ = false;
    wasClicked_ = false;
    isConverting_ = false;
    hasConvertElapsed_ = false;
    operationProgress_ = -1.0f;
    pendingLoadSuccessReport_ = false;
    pendingLoadErrorReport_ = false;
    ClearLastError();
    ClearLastConvertedPath();
    loader_.Start(info_.filePath);
}

void ConverterFileCardNode::Update(Rectangle bounds, Font font)
{
    (void)font;
    if (!hasFile_)
    {
        return;
    }

    if (isLoading_)
    {
        loader_.Update();
        if (loader_.HasResult())
        {
            const ConverterFileInfo result = loader_.GetResult();
            loader_.ClearResult();
            if (result.success)
            {
                SetInfo(result);
                pendingLoadSuccessReport_ = true;
                pendingLoadErrorReport_ = false;
            }
            else
            {
                info_ = result;
                hasFile_ = true;
                isLoading_ = false;
                pendingLoadErrorReport_ = true;
                pendingLoadSuccessReport_ = false;
                SetLastError(info_.error.empty() ? "Unknown load error." : info_.error);
            }
        }
    }

    LoadPreview();

    const Rectangle closeButton = CardChrome::CloseButtonBounds(bounds);
    isHovered_ = CheckCollisionPointRec(GetMousePosition(), bounds);
    wasClicked_ = false;
    wasConvertCancelClicked_ = false;
    wasCopyClicked_ = false;
    wasOpenPathClicked_ = false;

    if (isConverting_ && hasConvertStatusBounds_ && CheckCollisionPointRec(GetMousePosition(), convertStatusBounds_) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        wasConvertCancelClicked_ = true;
        return;
    }

    if (CheckCollisionPointRec(GetMousePosition(), closeButton) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        RequestClose();
        return;
    }

    if (!isLoading_ && CanCopyInfo())
    {
        const Rectangle copyButton = CardChrome::CopyButtonBounds(bounds);
        if (CheckCollisionPointRec(GetMousePosition(), copyButton) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            SetClipboardText(BuildCopyPayload().c_str());
            wasCopyClicked_ = true;
            return;
        }
    }

    if (CanRevealPath())
    {
        const Rectangle openPathButton = CardChrome::OpenPathButtonBounds(bounds);
        const Rectangle previewBounds = GetPreviewBounds(bounds);
        const Vector2 mouse = GetMousePosition();
        if ((CheckCollisionPointRec(mouse, openPathButton) || CheckCollisionPointRec(mouse, previewBounds)) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            wasOpenPathClicked_ = true;
            return;
        }
    }

    if (isHovered_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        wasClicked_ = true;
    }
}

void ConverterFileCardNode::Draw(Rectangle bounds, Font font, int displayIndex) const
{
    if (!hasFile_)
    {
        return;
    }

    const Rectangle animatedBounds = CardChrome::AnimatedBounds(bounds, pulseStartTime_, kPulseSeconds);
    const Color background =
        isSelected_ ? Color{18, 34, 18, 255} : (isHovered_ ? Color{14, 26, 14, 255} : Color{10, 18, 10, 255});
    Color border =
        isSelected_ ? Color{120, 178, 120, 255} : (isHovered_ ? Color{90, 124, 90, 255} : Color{64, 84, 64, 255});
    if (!useDefaultConvertSettings_)
    {
        if (isSelected_)
        {
            border = Color{230, 200, 80, 255};
        }
        else if (isHovered_)
        {
            border = Color{215, 185, 70, 255};
        }
        else
        {
            border = Color{200, 170, 60, 255};
        }
    }
    const Color titleColor = {240, 244, 240, 255};
    const Color metaColor = {150, 170, 150, 255};
    const float minSide = animatedBounds.width < animatedBounds.height ? animatedBounds.width : animatedBounds.height;
    const float roundness = 26.0f / minSide;

    DrawRectangleRounded(animatedBounds, roundness, 16, background);
    DrawBackgroundProgress(animatedBounds, roundness);
    DrawRectangleRoundedLines(animatedBounds, roundness, 16, border);
    if (isSelected_)
    {
        DrawRectangleRoundedLines({animatedBounds.x + 1.0f,
                                   animatedBounds.y + 1.0f,
                                   animatedBounds.width - 2.0f,
                                   animatedBounds.height - 2.0f},
                                  roundness,
                                  16,
                                  border);
    }

    const Rectangle iconBounds = GetPreviewBounds(animatedBounds);
    DrawRectangleRounded(iconBounds, kPreviewRoundness, kPreviewSegments, {28, 40, 28, 255});
    if (isLoading_)
    {
        DrawMiniSpinner({iconBounds.x + iconBounds.width * 0.5f, iconBounds.y + iconBounds.height * 0.5f});
    }
    else if (hasPreviewTexture_)
    {
        const Rectangle source = {
            0.0f, 0.0f, static_cast<float>(previewTexture_.width), static_cast<float>(previewTexture_.height)};
        DrawTexturePro(previewTexture_, source, iconBounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    else if (!info_.success)
    {
        DrawTextEx(font, "!", {iconBounds.x + 34.0f, iconBounds.y + 18.0f}, 28.0f, 0.0f, {232, 160, 150, 255});
    }
    else
    {
        DrawTextEx(font,
                   info_.container.c_str(),
                   {iconBounds.x + 10.0f, iconBounds.y + 24.0f},
                   18.0f,
                   0.0f,
                   {220, 232, 220, 255});
    }
    DrawRectangleRoundedLines(iconBounds, kPreviewRoundness, kPreviewSegments, {64, 84, 64, 255});
    if (isConverting_)
    {
        CardChrome::DrawPreviewElapsedOverlay(font, iconBounds, convertingElapsedSeconds_);
    }
    CardChrome::DrawPreviewIndexBadge(font, iconBounds, displayIndex);

    const float textX = animatedBounds.x + CardChrome::kTextXOffset;
    const float titleMaxWidth = CardChrome::TitleMaxWidth(bounds.width);

    if (isLoading_)
    {
        DrawTextEx(font, "Loading...", {textX, animatedBounds.y + 14.0f}, 18.0f, 0.0f, titleColor);
        CardChrome::DrawWrappedText(
            font, info_.fileName, {textX, animatedBounds.y + 38.0f}, 14.5f, titleMaxWidth, 2, metaColor);
        CardChrome::DrawCloseButton(animatedBounds, font);
        CardChrome::DrawCopyButton(animatedBounds, font, false);
        CardChrome::DrawOpenPathButton(animatedBounds, font, false);
        return;
    }

    if (!info_.success)
    {
        DrawTextEx(font, "Could not load video", {textX, animatedBounds.y + 14.0f}, 18.0f, 0.0f, {232, 160, 150, 255});
        CardChrome::DrawWrappedText(
            font, info_.fileName, {textX, animatedBounds.y + 38.0f}, 14.5f, titleMaxWidth, 2, metaColor);
        CardChrome::DrawCloseButton(animatedBounds, font);
        CardChrome::DrawCopyButton(animatedBounds, font, CanCopyInfo());
        CardChrome::DrawOpenPathButton(animatedBounds, font, CanRevealPath());
        return;
    }

    const bool revealPathAvailable = CanRevealPath();
    const Rectangle previewHitBounds = GetPreviewBounds(bounds);
    const Rectangle titleHitBounds = {animatedBounds.x, animatedBounds.y, bounds.width, animatedBounds.height};
    const bool titleHovered = CardChrome::IsTitleTextHovered(titleHitBounds, font, info_.fileName, 9.0f);
    const bool previewHovered = revealPathAvailable && CheckCollisionPointRec(GetMousePosition(), previewHitBounds);
    const Color drawTitleColor = titleHovered ? Color{210, 255, 210, 255} : titleColor;
    CardChrome::DrawWrappedText(
        font, info_.fileName, {textX, animatedBounds.y + 9.0f}, 16.0f, titleMaxWidth, 2, drawTitleColor);
    if (previewHovered)
    {
        UiCursor::RequestHand();
    }
    if (revealPathAvailable)
    {
        Tooltip::DrawIfHovered(font, previewHitBounds, "Open folder");
    }

    const std::string container = info_.container.empty() ? "Unknown" : info_.container;
    const std::string videoCodec = info_.videoCodec.empty() ? "None" : info_.videoCodec;
    const std::string audioCodec = info_.audioCodec.empty() ? "None" : info_.audioCodec;
    const std::string details = container + "  |  " + videoCodec + "  |  " + audioCodec;

    std::string statusText;
    const bool statusHovered =
        isConverting_ && hasConvertStatusBounds_ && CheckCollisionPointRec(GetMousePosition(), convertStatusBounds_);
    const bool showConvertSpinner = isConverting_ && !statusHovered;
    if (isConverting_)
    {
        statusText = statusHovered ? "Cancel" : "converting";
    }
    else if (hasConvertElapsed_)
    {
        statusText = "took " + FormatElapsed(convertElapsedSeconds_);
    }
    const bool showStatus = !statusText.empty();

    const float separatorWidth = MeasureTextEx(font, "  |  ", 14.0f, 0.0f).x;
    const float durationWidth = MeasureTextEx(font, info_.duration.c_str(), 14.0f, 0.0f).x;
    const float convertingTextWidth = MeasureTextEx(font, "converting", 14.0f, 0.0f).x;
    const float cancelTextWidth = MeasureTextEx(font, "Cancel", 14.0f, 0.0f).x;
    const float convertActionSlotWidth = std::max(convertingTextWidth + 16.0f, cancelTextWidth);
    const float statusTextWidth = showStatus ? MeasureTextEx(font, statusText.c_str(), 14.0f, 0.0f).x : 0.0f;
    const float layoutStatusWidth = isConverting_ ? convertActionSlotWidth : statusTextWidth;
    const float statusWidth = showStatus ? separatorWidth + layoutStatusWidth : 0.0f;
    const float durationBlockWidth = separatorWidth + 20.0f + durationWidth;
    const float metaMaxX = animatedBounds.x + animatedBounds.width - 34.0f;
    const float maxDetailsWidth = std::max(0.0f, metaMaxX - textX - durationBlockWidth - statusWidth);
    const float detailsWidth = std::min(MeasureTextEx(font, details.c_str(), 14.0f, 0.0f).x, maxDetailsWidth);

    UiClip::Push({textX, animatedBounds.y + 48.0f, maxDetailsWidth, 20.0f});
    DrawTextEx(font, details.c_str(), {textX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);
    UiClip::Pop();

    const float durationSeparatorX = textX + detailsWidth;
    DrawTextEx(font, "  |  ", {durationSeparatorX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);

    const Vector2 clockCenter = {durationSeparatorX + separatorWidth + 8.0f, animatedBounds.y + 58.0f};
    DrawCircleLines(static_cast<int>(clockCenter.x), static_cast<int>(clockCenter.y), 5.0f, metaColor);
    DrawLine(static_cast<int>(clockCenter.x),
             static_cast<int>(clockCenter.y),
             static_cast<int>(clockCenter.x),
             static_cast<int>(clockCenter.y - 3.0f),
             metaColor);
    DrawLine(static_cast<int>(clockCenter.x),
             static_cast<int>(clockCenter.y),
             static_cast<int>(clockCenter.x + 2.0f),
             static_cast<int>(clockCenter.y + 2.0f),
             metaColor);
    const Vector2 durationPosition = {clockCenter.x + 12.0f, animatedBounds.y + 49.0f};
    DrawTextEx(font, info_.duration.c_str(), durationPosition, 14.0f, 0.0f, metaColor);

    hasConvertStatusBounds_ = false;
    if (showStatus)
    {
        const float separatorX = durationPosition.x + durationWidth + 4.0f;
        DrawTextEx(font, "  |  ", {separatorX, animatedBounds.y + 49.0f}, 14.0f, 0.0f, metaColor);
        const float statusSlotStart = separatorX + separatorWidth + 4.0f;
        const Color statusColor = statusHovered ? Color{240, 96, 86, 255} : metaColor;
        if (isConverting_)
        {
            convertStatusBounds_ = {
                statusSlotStart - 3.0f, animatedBounds.y + 45.0f, convertActionSlotWidth + 6.0f, 24.0f};
            hasConvertStatusBounds_ = true;
        }
        DrawTextEx(font, statusText.c_str(), {statusSlotStart, animatedBounds.y + 49.0f}, 14.0f, 0.0f, statusColor);
        if (statusHovered)
        {
            UiCursor::RequestHand();
        }
        if (showConvertSpinner)
        {
            DrawMiniSpinner({statusSlotStart + convertingTextWidth + 8.0f, animatedBounds.y + 58.0f});
        }
    }

    const float metaY = animatedBounds.y + 49.0f;
    const float metaHeight = 18.0f;
    float metaCursorX = textX;
    TryDrawMetaSegmentTooltip(font, container, "File Format", metaCursorX, metaY, metaHeight);
    metaCursorX += separatorWidth;
    TryDrawMetaSegmentTooltip(font, videoCodec, "Video Codec", metaCursorX, metaY, metaHeight);
    metaCursorX += separatorWidth;
    TryDrawMetaSegmentTooltip(font, audioCodec, "Audio Codec", metaCursorX, metaY, metaHeight);

    const Rectangle durationBounds = {
        clockCenter.x - 6.0f, metaY - 2.0f, (durationPosition.x + durationWidth) - (clockCenter.x - 6.0f), metaHeight};
    Tooltip::DrawIfHovered(font, durationBounds, "Duration");

    CardChrome::DrawCloseButton(animatedBounds, font);
    CardChrome::DrawCopyButton(animatedBounds, font, CanCopyInfo());
    CardChrome::DrawOpenPathButton(animatedBounds, font, CanRevealPath());
}

void ConverterFileCardNode::Clear()
{
    if (isLoading_)
    {
        loader_.Cancel();
    }
    UnloadPreview();
    hasFile_ = false;
    isLoading_ = false;
    shouldClose_ = false;
    isSelected_ = false;
    wasClicked_ = false;
    triedLoadingPreview_ = false;
    pendingLoadSuccessReport_ = false;
    pendingLoadErrorReport_ = false;
    lastErrorText_.clear();
    lastConvertedPath_.clear();
    info_ = {};
}

void ConverterFileCardNode::SetSelected(bool selected)
{
    isSelected_ = selected;
}

void ConverterFileCardNode::SetConverting()
{
    isConverting_ = true;
    hasConvertElapsed_ = false;
    convertElapsedSeconds_ = 0.0;
    convertingElapsedSeconds_ = 0.0;
    operationProgress_ = 0.04f;
    ClearLastError();
    ClearLastConvertedPath();
}

void ConverterFileCardNode::ClearConverting()
{
    isConverting_ = false;
    convertingElapsedSeconds_ = 0.0;
    operationProgress_ = -1.0f;
}

void ConverterFileCardNode::SetConvertingElapsed(double seconds)
{
    convertingElapsedSeconds_ = seconds;
}

void ConverterFileCardNode::SetConvertElapsed(double seconds)
{
    convertElapsedSeconds_ = seconds;
    hasConvertElapsed_ = true;
    isConverting_ = false;
    operationProgress_ = -1.0f;
}

void ConverterFileCardNode::SetLastConvertedPath(std::string path)
{
    lastConvertedPath_ = std::move(path);
}

void ConverterFileCardNode::ClearLastConvertedPath()
{
    lastConvertedPath_.clear();
}

bool ConverterFileCardNode::IsConverting() const
{
    return isConverting_;
}

bool ConverterFileCardNode::HasCompletedConvert() const
{
    return hasConvertElapsed_;
}

double ConverterFileCardNode::ConvertElapsedSeconds() const
{
    return convertElapsedSeconds_;
}

const std::string& ConverterFileCardNode::LastConvertedPath() const
{
    return lastConvertedPath_;
}

void ConverterFileCardNode::SetOperationProgress(float progress)
{
    operationProgress_ = std::clamp(progress, 0.0f, 1.0f);
}

void ConverterFileCardNode::ClearOperationProgress()
{
    operationProgress_ = -1.0f;
}

void ConverterFileCardNode::SetLastError(std::string error)
{
    lastErrorText_ = std::move(error);
}

void ConverterFileCardNode::ClearLastError()
{
    lastErrorText_.clear();
}

void ConverterFileCardNode::TriggerPulse()
{
    pulseStartTime_ = GetTime();
}

bool ConverterFileCardNode::HasFile() const
{
    return hasFile_;
}

bool ConverterFileCardNode::IsLoading() const
{
    return isLoading_;
}

bool ConverterFileCardNode::HasFilePath(const std::string& filePath) const
{
    return hasFile_ && info_.filePath == filePath;
}

bool ConverterFileCardNode::ShouldClose() const
{
    return shouldClose_;
}

void ConverterFileCardNode::RequestClose()
{
    if (isLoading_)
    {
        dismissedDuringLoad_ = true;
        CancelLoading();
    }
    shouldClose_ = true;
}

bool ConverterFileCardNode::WasDismissedDuringLoad() const
{
    return dismissedDuringLoad_;
}

bool ConverterFileCardNode::IsHovered() const
{
    return isHovered_;
}

bool ConverterFileCardNode::WasConvertCancelClicked() const
{
    return wasConvertCancelClicked_;
}

bool ConverterFileCardNode::WasClicked() const
{
    return wasClicked_;
}

bool ConverterFileCardNode::WasCopyClicked() const
{
    return wasCopyClicked_;
}

bool ConverterFileCardNode::WasOpenPathClicked() const
{
    return wasOpenPathClicked_;
}

bool ConverterFileCardNode::IsSelected() const
{
    return isSelected_;
}

bool ConverterFileCardNode::TryConsumeLoadSuccess()
{
    if (!pendingLoadSuccessReport_)
    {
        return false;
    }

    pendingLoadSuccessReport_ = false;
    return true;
}

bool ConverterFileCardNode::TryConsumeLoadFailure(std::string& error)
{
    if (!pendingLoadErrorReport_)
    {
        return false;
    }

    pendingLoadErrorReport_ = false;
    pendingLoadSuccessReport_ = false;
    error = info_.error.empty() ? "Unknown load error." : info_.error;
    return true;
}

void ConverterFileCardNode::CancelLoading()
{
    if (!isLoading_)
    {
        return;
    }

    loader_.Cancel();
    isLoading_ = false;
}

bool ConverterFileCardNode::CanCopyInfo() const
{
    if (isLoading_ || isConverting_)
    {
        return false;
    }
    return info_.success || !lastErrorText_.empty() || !info_.error.empty() || hasConvertElapsed_;
}

bool ConverterFileCardNode::CanRevealPath() const
{
    return !isLoading_ && hasConvertElapsed_ && !ResolvePathForReveal().empty();
}

std::string ConverterFileCardNode::BuildCopyPayload() const
{
    if (!lastErrorText_.empty() || !info_.error.empty())
    {
        std::string payload;
        if (!info_.filePath.empty())
        {
            payload += "File: ";
            payload += info_.filePath;
            payload += "\n\n";
        }
        payload += lastErrorText_.empty() ? info_.error : lastErrorText_;
        return payload;
    }

    const bool hasOutput = !lastConvertedPath_.empty();
    std::string displayName = info_.fileName;
    std::string displayContainer = info_.container.empty() ? "Unknown" : info_.container;
    if (hasOutput)
    {
        const std::filesystem::path outputPath = std::filesystem::u8path(lastConvertedPath_);
        displayName = outputPath.filename().u8string();
        std::string ext = outputPath.extension().u8string();
        if (!ext.empty() && ext.front() == '.')
        {
            ext.erase(ext.begin());
        }
        for (char& ch : ext)
        {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        if (ext == "M4V")
        {
            displayContainer = "MP4";
        }
        else if (!ext.empty())
        {
            displayContainer = ext;
        }
    }

    std::string payload = "4KDowner convert info\n";
    if (!info_.filePath.empty())
    {
        payload += "Input: ";
        payload += info_.filePath;
        payload += '\n';
    }
    if (hasOutput)
    {
        payload += "Output: ";
        payload += lastConvertedPath_;
        payload += '\n';
    }
    if (!displayName.empty())
    {
        payload += "Name: ";
        payload += displayName;
        payload += '\n';
    }
    payload += "Container: ";
    payload += displayContainer;
    payload += "\nVideo: ";
    payload += info_.videoCodec.empty() ? "None" : info_.videoCodec;
    payload += "\nAudio: ";
    payload += info_.audioCodec.empty() ? "None" : info_.audioCodec;
    if (!info_.duration.empty())
    {
        payload += "\nDuration: ";
        payload += info_.duration;
    }
    if (hasConvertElapsed_)
    {
        const int totalSeconds = static_cast<int>(convertElapsedSeconds_ + 0.5);
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", totalSeconds / 60, totalSeconds % 60);
        payload += "\nTook: ";
        payload += buffer;
    }
    return payload;
}

std::string ConverterFileCardNode::ResolvePathForReveal() const
{
    if (lastConvertedPath_.empty())
    {
        return {};
    }

    std::error_code error;
    const std::filesystem::path path = std::filesystem::u8path(lastConvertedPath_);
    if (std::filesystem::exists(path, error))
    {
        return lastConvertedPath_;
    }

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty() && std::filesystem::is_directory(parent, error))
    {
        return parent.u8string();
    }
    return {};
}

const ConverterFileInfo& ConverterFileCardNode::Info() const
{
    return info_;
}

void ConverterFileCardNode::LoadPreview()
{
    if (hasPreviewTexture_ || triedLoadingPreview_ || isLoading_ || info_.previewPath.empty())
    {
        return;
    }

    triedLoadingPreview_ = true;
    std::error_code error;
    const std::filesystem::path previewPath = std::filesystem::u8path(info_.previewPath);
    if (!std::filesystem::exists(previewPath, error))
    {
        return;
    }

    Image image = LoadImage(previewPath.string().c_str());
    if (image.data == nullptr)
    {
        return;
    }

    PreparePreviewImage(image);
    if (image.data == nullptr || image.width <= 0 || image.height <= 0)
    {
        UnloadImage(image);
        return;
    }

    previewTexture_ = LoadTextureFromImage(image);
    UnloadImage(image);
    hasPreviewTexture_ = previewTexture_.id != 0;
    if (hasPreviewTexture_)
    {
        SetTextureFilter(previewTexture_, TEXTURE_FILTER_BILINEAR);
    }
}

void ConverterFileCardNode::UnloadPreview()
{
    if (hasPreviewTexture_)
    {
        UnloadTexture(previewTexture_);
        previewTexture_ = {};
        hasPreviewTexture_ = false;
    }
}

void ConverterFileCardNode::DrawBackgroundProgress(Rectangle bounds, float roundness) const
{
    if (operationProgress_ < 0.0f)
    {
        return;
    }

    const float fillWidth = bounds.width * operationProgress_;
    if (fillWidth <= 0.5f)
    {
        return;
    }

    UiClip::Push({bounds.x, bounds.y, fillWidth, bounds.height});
    DrawRectangleRounded(bounds, roundness, 16, Color{52, 92, 148, 120});
    UiClip::Pop();
}

bool ConverterFileCardNode::UseDefaultConvertSettings() const
{
    return useDefaultConvertSettings_;
}

void ConverterFileCardNode::SetUseDefaultConvertSettings(bool useDefault)
{
    useDefaultConvertSettings_ = useDefault;
}

ConverterOptions& ConverterFileCardNode::CustomConvertOptions()
{
    return customOptions_;
}

const ConverterOptions& ConverterFileCardNode::CustomConvertOptions() const
{
    return customOptions_;
}

void ConverterFileCardNode::DrawMiniSpinner(Vector2 center) const
{
    const double time = GetTime();
    const int segments = 8;
    for (int index = 0; index < segments; ++index)
    {
        const float angle = static_cast<float>(time * 6.0 + index * (6.2831853 / segments));
        const float alpha = static_cast<float>(index + 1) / static_cast<float>(segments);
        const Vector2 start = {center.x + std::cos(angle) * 4.0f, center.y + std::sin(angle) * 4.0f};
        const Vector2 end = {center.x + std::cos(angle) * 7.0f, center.y + std::sin(angle) * 7.0f};
        DrawLineEx(start, end, 1.5f, Color{160, 178, 160, static_cast<unsigned char>(70 + alpha * 150)});
    }
}

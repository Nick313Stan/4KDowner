#pragma once

#include "Button.h"
#include "Checkbox.h"
#include "ConverterFileCardNode.h"
#include "ConvertRunner.h"
#include "DownloadRunner.h"
#include "Dropdown.h"
#include "LinkCardNode.h"
#include "PathField.h"

#include "raylib.h"

#include <array>
#include <string>
#include <vector>

class DockArea {
public:
    DockArea();

    void Update(int windowWidth, int windowHeight, Font font);
    void Draw(int windowWidth, int windowHeight, Font font) const;
    void UnloadResources();

private:
    enum class Workspace {
        Downloader,
        Converter
    };

    enum class FooterNotificationScope {
        Any,
        Downloader,
        Converter
    };

    static constexpr int kMaxParallelDownloads = 2;
    static constexpr int kMaxParallelConverts = 2;

    Rectangle GetBounds(int windowWidth, int windowHeight) const;
    Rectangle GetLeftPanel(int windowWidth, int windowHeight) const;
    Rectangle GetRightPanel(int windowWidth, int windowHeight) const;
    Rectangle GetHeader(int windowWidth) const;
    Rectangle GetFooter(int windowWidth, int windowHeight) const;
    Rectangle GetRightSettingsPanel(Rectangle rightPanel) const;
    Rectangle GetGlobalPathPanel(Rectangle rightPanel) const;
    Rectangle GetInsertLinkButtonBounds(Rectangle leftPanel) const;
    Rectangle GetChooseFileButtonBounds(Rectangle leftPanel) const;
    Rectangle GetListActionButtonBounds(Rectangle leftPanel, int index, float scrollOffset) const;
    Rectangle GetDownloadButtonBounds(Rectangle settingsPanel) const;
    Rectangle GetSecondaryActionButtonBounds(Rectangle settingsPanel) const;
    Rectangle GetCardBounds(Rectangle leftPanel, int index, float scrollOffset = 0.0f) const;
    float GetMaxCardScroll(Rectangle leftPanel, int itemCount, float reservedBottom = 0.0f) const;
    void UpdateCardScroll(Rectangle leftPanel, int itemCount, float reservedBottom, float& scrollOffset) const;

    void UpdateHeader();
    void UpdateDownloaderWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font);
    void UpdateConverterWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font);
    void DrawDownloaderWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font) const;
    void DrawConverterWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font) const;
    void HandleChooseFileRequest();
    void HandleInsertLinkRequest();
    void SyncCardProgress();
    void UpdateCards(Rectangle leftPanel);
    void OnCardClosed(const std::string& url);
    void UpdateRightPanel(Rectangle rightPanel, Font font);
    void HandleDownloadRequest();
    void HandleDownloadAllRequest();
    bool HasDownloadableIdleCards() const;
    bool HasValidDownloadCards() const;
    bool CanDownloadSelected() const;
    void RemoveFromDownloadQueue(const std::string& url);
    void ClearBatchQueueStates();
    bool BuildDownloadRequestForCard(LinkCardNode& card, DownloadRequest& request);
    bool PrepareDownloadRequest(DownloadRequest& request);
    bool StartNextPendingDownload();
    void StartDownload(DownloadRequest request);
    void HandleConvertRequest();
    void HandleConvertAllRequest();
    bool BuildConvertRequestForCard(const ConverterFileCardNode& card, ConvertRequest& request) const;
    bool PrepareConvertRequest(ConvertRequest& request);
    bool StartNextPendingConvert();
    void StartConvert(ConvertRequest request);
    void RemovePendingConvertsForPath(const std::string& inputPath);
    void CancelConverterCard(const std::string& inputPath);
    void PulseConverterFooterHint();
    void ClearFooterNotification();
    void ShowFooterNotification(
        const std::string& text,
        FooterNotificationScope scope = FooterNotificationScope::Any,
        const std::string& errorLog = "",
        const std::string& clipboardLog = "");
    void UpdateFooterNotificationTimer();
    void UpdateOverwritePrompt(int windowWidth, int windowHeight);
    void UpdateAboutDialog(int windowWidth, int windowHeight, Font font);
    void DrawRightPanel(Rectangle rightPanel, Font font) const;
    void DrawHeader(Rectangle header, Font font) const;
    void UpdateFooter();
    void DrawFooter(Rectangle footer, Font font) const;
    bool BuildFooterNotification(std::string& status, bool& useConvertStatus, bool& isRunning) const;
    std::string BuildDownloadFooterErrorLog(const DownloadRunner& runner, const std::string& summary) const;
    std::string BuildConvertFooterErrorLog(const ConvertRunner& runner, const std::string& summary) const;
    void CollectParseFailures();
    void CollectConverterLoadResults();
    void AppendFooterDiagnosticsForCard(const std::string& url, const std::string& downloadReport);
    const LinkCardNode* FindCardByUrl(const std::string& url) const;
    static bool IsFooterErrorStatus(const std::string& status, bool isRunning);
    void DrawFooterCloseIcon(Rectangle bounds, bool hovered) const;
    void DrawFooterCopyIcon(Rectangle bounds, bool hovered) const;
    void DrawOverwritePrompt(int windowWidth, int windowHeight, Font font) const;
    void DrawAboutDialog(int windowWidth, int windowHeight, Font font) const;
    static std::string GetDefaultDownloadPath();
    LinkCardNode* GetSelectedCard();
    const LinkCardNode* GetSelectedCard() const;
    ConverterFileCardNode* GetSelectedConverterCard();
    const ConverterFileCardNode* GetSelectedConverterCard() const;

    bool AnyDownloadRunning() const;
    bool AnyConvertRunning() const;
    int RunningDownloadCount() const;
    int RunningConvertCount() const;
    DownloadRunner* FirstFreeDownloadRunner();
    ConvertRunner* FirstFreeConvertRunner();
    DownloadRunner* FindDownloadRunnerByUrl(const std::string& url);
    ConvertRunner* FindConvertRunnerByPath(const std::string& inputPath);
    void CancelAllDownloads();
    void CancelAllConverts();
    void ProcessFinishedDownloadRunner(DownloadRunner& runner);
    void ProcessFinishedConvertRunner(ConvertRunner& runner);

    Button insertLinkButton_{"Insert Link"};
    Button chooseFileButton_{"Choose File"};
    Button downloadButton_{"Download Selected"};
    Button downloadAllButton_{"Download All"};
    Button convertButton_{"Convert Selected"};
    Button convertAllButton_{"Convert All"};
    Button cancelDownloadButton_{"Cancel"};
    Button replaceFileButton_{"Replace"};
    Button cancelReplaceButton_{"Cancel"};
    Button cancelAllReplaceButton_{"Cancel All"};
    Button closeAboutButton_{"OK"};
    Dropdown fileFormatDropdown_{{"MP4"}};
    Dropdown mediaModeDropdown_{{"Both", "Video only", "Audio only"}};
    Dropdown qualityDropdown_{{"2160p"}};
    Dropdown convertContainerDropdown_{{"MP4", "MKV", "MOV", "WEBM"}};
    Dropdown convertVideoDropdown_{{"H.264", "H.265", "AV1", "VP9"}};
    Dropdown convertAudioDropdown_{{"AAC", "MP3", "Opus", "FLAC"}};
    Checkbox customPathCheckbox_;
    Checkbox convertContainerCheckbox_;
    Checkbox convertVideoCheckbox_;
    Checkbox convertAudioCheckbox_;
    PathField customPathField_;
    PathField globalPathField_;
    std::array<DownloadRunner, kMaxParallelDownloads> downloadRunners_;
    std::array<ConvertRunner, kMaxParallelConverts> convertRunners_;
    std::vector<ConverterFileCardNode> converterCards_;
    std::vector<LinkCardNode> cards_;
    std::string globalDownloadPath_;
    std::vector<DownloadRequest> pendingDownloadQueue_;
    std::vector<ConvertRequest> pendingConvertQueue_;
    DownloadRequest pendingOverwriteRequest_;
    ConvertRequest pendingOverwriteConvertRequest_;
    std::string pendingOverwriteFileName_;
    double nextDownloadStartTime_ = 0.0;
    float downloaderScrollOffset_ = 0.0f;
    float converterScrollOffset_ = 0.0f;
    Workspace activeWorkspace_ = Workspace::Downloader;
    bool isOverwritePromptOpen_ = false;
    bool isAboutDialogOpen_ = false;
    bool overwritePromptIsConvert_ = false;
    bool isBatchDownloading_ = false;
    bool isBatchConverting_ = false;
    double batchConvertElapsedTotal_ = 0.0;
    std::string lastConverterDropdownCardPath_;
    int lastConverterSelectionAnchor_ = -1;
    bool overwriteAllExisting_ = false;
    bool convertContainer_ = false;
    bool convertVideo_ = false;
    bool convertAudio_ = false;
    int convertContainerIndex_ = 0;
    int convertVideoIndex_ = 0;
    int convertAudioIndex_ = 0;
    bool footerNotificationDismissed_ = false;
    std::string footerNotificationText_;
    bool footerNotificationVisible_ = false;
    double footerNotificationShowTime_ = -1.0;
    double footerNotificationHideTime_ = -1.0;
    FooterNotificationScope footerNotificationScope_ = FooterNotificationScope::Any;
    std::string errorConsoleLog_;
    std::string footerClipboardLog_;
    mutable Rectangle footerCloseButtonBounds_{};
    mutable Rectangle footerCopyButtonBounds_{};
    mutable bool footerNotificationShown_ = false;
    mutable bool footerCopyVisible_ = false;

    static constexpr float kMargin = 5.0f;
    static constexpr float kGap = 5.0f;
    static constexpr float kCornerRadius = 6.5f;
    static constexpr float kLeftPanelRatio = 0.6f;
    static constexpr float kCardHeight = 75.0f;
    static constexpr float kHeaderHeight = 25.0f;
    static constexpr float kFooterHeight = 25.0f;
    static constexpr float kFooterNotificationMargin = 1.0f;
    static constexpr double kFooterNotificationDelaySeconds = 0.15;
    static constexpr double kFooterNotificationAutoHideSeconds = 3.0;
};

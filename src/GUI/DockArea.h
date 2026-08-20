#pragma once

#include "Button.h"
#include "Checkbox.h"
#include "ConvertRunner.h"
#include "ConverterFileCardNode.h"
#include "DownloaderListItemInclude.h"
#include "DownloadRunner.h"
#include "Dropdown.h"
#include "FoldoutPanel.h"
#include "LinkCardNode.h"
#include "PathField.h"
#include "ShortcutRouter.h"
#include "UndoStack.h"

#include "raylib.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class DockArea
{
public:
    DockArea();

    void Update(int windowWidth, int windowHeight, Font font);
    void Draw(int windowWidth, int windowHeight, Font font, Font fontFooterAa = {}) const;
    void UnloadResources();

    // Undo command apply API (also used by UndoStack commands).
    void UndoRestoreLinkCard(const LinkCardUndoSnapshot& snapshot);
    void UndoRemoveLinkCardByUrl(const std::string& url);
    void UndoRestoreLinkGroupChild(const LinkGroupChildUndoSnapshot& snapshot);
    void UndoRemoveLinkGroupChild(const LinkGroupChildUndoSnapshot& snapshot);
    void UndoRestoreConverterCard(const ConverterCardUndoSnapshot& snapshot);
    void UndoRemoveConverterCardByPath(const std::string& filePath);
    void UndoApplyCardOptions(const std::string& url, const DownloadOptions& options);
    void UndoApplyConverterSettings(const ConverterSettingsSnapshot& settings);
    void UndoApplyConverterCardOptions(const std::string& filePath, const ConverterCardOptionsSnapshot& snapshot);
    void UndoApplyGlobalPath(const std::string& path);
    void UndoApplyGlobalAutoConvert(const AutoConvertOptions& options);
    void UndoApplyLinkCustomAutoConvert(const std::vector<LinkCustomAutoConvertSnapshot>& snapshots);
    void UndoApplyLinkExcludeFlags(const std::vector<LinkExcludeFlag>& flags);
    void UndoInvokeDownloadAll();
    void UndoInvokeCancelAllDownloads();
    void UndoInvokeConvertAll();
    void UndoInvokeCancelAllConverts();

private:
    enum class Workspace
    {
        Downloader,
        Converter
    };

    enum class FooterNotificationScope
    {
        Any,
        Downloader,
        Converter
    };

    static constexpr int kMaxParallelDownloads = 3;
    static constexpr int kMaxParallelConverts = 3;

    Rectangle GetBounds(int windowWidth, int windowHeight) const;
    Rectangle GetLeftPanel(int windowWidth, int windowHeight) const;
    Rectangle GetRightPanel(int windowWidth, int windowHeight) const;
    Rectangle GetHeader(int windowWidth) const;
    Rectangle GetFooter(int windowWidth, int windowHeight) const;
    Rectangle GetRightSettingsPanel(Rectangle rightPanel) const;
    Rectangle GetAutoConvertDockPanel(Rectangle rightPanel) const;
    Rectangle GetConverterDefaultDockPanel(Rectangle rightPanel) const;
    Rectangle GetGlobalPathPanel(Rectangle rightPanel) const;
    Rectangle GetInsertLinkButtonBounds(Rectangle leftPanel) const;
    Rectangle GetChooseFileButtonBounds(Rectangle leftPanel) const;
    Rectangle GetListActionButtonBounds(Rectangle leftPanel, int index, float scrollOffset) const;
    Rectangle GetDownloadButtonBounds(Rectangle settingsPanel) const;
    Rectangle GetSecondaryActionButtonBounds(Rectangle settingsPanel) const;
    Rectangle GetCardBounds(Rectangle leftPanel, int index, float scrollOffset = 0.0f) const;
    void RebuildDownloaderLayoutCache() const;
    float GetDownloaderItemTop(Rectangle leftPanel, int index, float scrollOffset) const;
    float GetDownloaderItemHeight(int index) const;
    float GetDownloaderListContentHeight() const;
    float GetDownloaderReservedRight(Rectangle leftPanel) const;
    Rectangle GetDownloaderSingleBounds(Rectangle leftPanel, int index, float scrollOffset) const;
    Rectangle GetDownloaderGroupHeaderBounds(Rectangle leftPanel, int index, float scrollOffset) const;
    Rectangle
    GetDownloaderGroupChildBounds(Rectangle leftPanel, int itemIndex, int childIndex, float scrollOffset) const;
    Rectangle GetDownloaderGroupLoadMoreBounds(Rectangle leftPanel, int itemIndex, float scrollOffset) const;
    Rectangle GetDownloaderActionSlotBounds(Rectangle leftPanel, float scrollOffset) const;
    LinkCardNode* FindLinkCardByUrl(const std::string& url);
    const LinkCardNode* FindLinkCardByUrl(const std::string& url) const;
    void ForEachLinkCard(const std::function<void(LinkCardNode&)>& visitor);
    void ForEachLinkCard(const std::function<void(const LinkCardNode&)>& visitor) const;
    int CountDownloaderSelectableUnits() const;
    float GetCardListScrollbarReserve(Rectangle leftPanel, int itemCount) const;
    float GetMaxCardScroll(Rectangle leftPanel, float contentHeight, float reservedBottom = 0.0f) const;
    void UpdateCardScroll(Rectangle leftPanel, float contentHeight, float reservedBottom, float& scrollOffset) const;

    void UpdateHeader();
    void UpdateDownloaderWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font);
    void UpdateConverterWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font);
    void HandleShortcuts(Rectangle leftPanel, Rectangle rightPanel);
    HoverContext ResolveHoverTarget(Rectangle leftPanel, Rectangle rightPanel) const;
    bool HandleFoldoutHoverShortcuts();
    void OnFoldoutCollapsedByShortcut(FoldoutPanel& foldout);
    void NavigateDownloaderSelection(int delta, Rectangle leftPanel, bool allowModifiers = true);
    void NavigateConverterSelection(int delta, Rectangle leftPanel, bool allowModifiers = true);
    void EnsureCardVisibleInList(Rectangle leftPanel, int index, float& scrollOffset) const;
    void CloseLinkCardAt(int index);
    void CloseConverterCardAt(int index);
    void RecordLinkCardRemoval(int index);
    void RecordConverterCardRemoval(int index);
    bool CanRecordLinkCardRemoval(int index) const;
    bool CanRecordConverterCardRemoval(int index) const;
    void FlushPendingBatchLinkRemoveUndo();
    void FlushPendingBatchConverterRemoveUndo();
    LinkCardUndoSnapshot CaptureLinkCardSnapshot(int index) const;
    ConverterCardUndoSnapshot CaptureConverterCardSnapshot(int index) const;
    ConverterSettingsSnapshot CaptureConverterSettings() const;
    void PushUndo(std::unique_ptr<UndoCommand> command);
    void PerformUndo();
    void PerformRedo();
    void DrawDownloaderWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font) const;
    void DrawConverterWorkspace(Rectangle leftPanel, Rectangle rightPanel, Font font) const;
    void HandleChooseFileRequest();
    void HandleInsertLinkRequest(bool allowDuplicate = false);
    void HandleSeedTestLinksRequest();
    void HandleSeed8kLinkRequest();
    Rectangle GetFooterSeedButtonBounds(Rectangle footer) const;
    Rectangle GetFooterSeed8kButtonBounds(Rectangle footer) const;
    void SyncCardProgress();
    void UpdateCards(Rectangle leftPanel, Font font);
    void OnCardClosed(const std::string& url);
    void UpdateRightPanel(Rectangle rightPanel, Font font, bool blockByUpperOverlay = false);
    bool UpdateAutoConvertDock(Rectangle autoConvertPanel, Font font);
    void
    DrawAutoConvertDock(Rectangle autoConvertPanel, Font font, bool drawControls = true, bool drawPopups = true) const;
    bool UpdateConverterDefaultDock(Rectangle defaultDockPanel, Font font);
    void DrawConverterDefaultDock(Rectangle defaultDockPanel,
                                  Font font,
                                  bool drawControls = true,
                                  bool drawPopups = true) const;
    void UpdateConverterCardOptions(Rectangle settingsPanel, Font font, bool blockByUpperOverlay = false);
    void DrawConverterCardOptions(Rectangle settingsPanel, Font font) const;
    std::vector<ConverterFileCardNode*> CollectEditableSelectedConverterCards();
    std::string BuildConverterCardOptionsSelectionKey() const;
    void SyncConverterCardOptionsEditFromSelection(const std::vector<ConverterFileCardNode*>& cards);
    void ApplyConverterCardOptionsEditToCards(const std::vector<ConverterFileCardNode*>& cards);
    std::vector<ConverterCardOptionsSnapshot>
    CaptureConverterCardOptionsSnapshots(const std::vector<ConverterFileCardNode*>& cards) const;
    ConverterSettingsSnapshot ResolveConverterSettingsForCard(const ConverterFileCardNode& card) const;
    int CountSelectedConverterCards() const;
    bool CanBuildAnyConvertRequest() const;
    void HandleDownloadRequest();
    void HandleDownloadAllRequest();
    bool HasDownloadableIdleCards() const;
    bool HasValidDownloadCards() const;
    bool CanDownloadSelected() const;
    bool SelectedCardShowsCancel() const;
    void HandleCancelSelectedRequest();
    void HandleCancelAllDownloadsRequest();
    void HandleCancelAllConvertsRequest();
    bool SelectedConverterShowsCancel() const;
    bool IsConverterCardQueued(const std::string& inputPath) const;
    bool HasActiveConverterWorkspaceWork() const;
    bool HasActiveDownloadWorkspaceWork() const;
    void SyncConverterBusyStateAfterCardChanges();
    void HandleCancelSelectedConvertsRequest();
    void RemoveFromDownloadQueue(const std::string& url);
    void PrioritizeDownload(const std::string& url);
    DownloadRunner* FindLowestProgressDownloadRunner();
    void ClearBatchQueueStates();
    bool BuildDownloadRequestForCard(LinkCardNode& card, DownloadRequest& request);
    bool PrepareDownloadRequest(DownloadRequest& request);
    bool StartNextPendingDownload();
    void StartDownload(DownloadRequest request);
    void HandleConvertRequest();
    void HandleConvertAllRequest();
    bool BuildConvertRequestForCard(const ConverterFileCardNode& card, ConvertRequest& request) const;
    bool BuildAutoConvertRequestForCard(const LinkCardNode& card, ConvertRequest& request) const;
    AutoConvertOptions ResolveAutoConvertOptionsForCard(const LinkCardNode& card) const;
    bool IsAutoConvertActiveForDownload() const;
    static std::string GetAutoConvertStagingPath();
    static std::string PredictAutoConvertExtension(const AutoConvertOptions& options,
                                                   const std::string& downloadFormat);
    bool PrepareConvertRequest(ConvertRequest& request);
    bool StartNextPendingConvert();
    void StartConvert(ConvertRequest request);
    void QueueAutoConvertForCard(LinkCardNode& card);
    bool AnyLinkCardConverting() const;
    void CancelLinkCardConvert(const std::string& inputPath);
    void RemovePendingConvertsForPath(const std::string& inputPath);
    void CancelConverterCard(const std::string& inputPath);
    void PulseConverterFooterHint();
    void ClearFooterNotification();
    void ShowFooterNotification(const std::string& text,
                                FooterNotificationScope scope = FooterNotificationScope::Any,
                                const std::string& errorLog = "",
                                const std::string& clipboardLog = "");
    void UpdateFooterNotificationTimer();
    void UpdateOverwritePrompt(int windowWidth, int windowHeight);
    void UpdateAboutDialog(int windowWidth, int windowHeight, Font font);
    void UpdateInfoDialog(int windowWidth, int windowHeight, Font font);
    void DrawRightPanel(Rectangle rightPanel, Font font) const;
    void DrawHeader(Rectangle header, Font font) const;
    void UpdateFooter();
    void DrawFooter(Rectangle footer, Font font, Font fontFooterAa = {}) const;
    bool BuildFooterNotification(std::string& status, bool& useConvertStatus, bool& isRunning) const;
    std::string BuildDownloadFooterErrorLog(const DownloadRunner& runner, const std::string& summary) const;
    std::string BuildConvertFooterErrorLog(const ConvertRunner& runner, const std::string& summary) const;
    void CollectParseFailures();
    double SumCompletedCardDownloadElapsed() const;
    void CollectConverterLoadResults();
    void AppendFooterDiagnosticsForCard(const std::string& url, const std::string& downloadReport);
    void MaybeShowDownloadConvertBatchFinished();
    void CleanupLinkCardAutoConvertStaging(LinkCardNode& card, const std::string& fallbackInputPath);
    void EnqueuePendingStagingCleanup(const std::string& path);
    void ProcessPendingStagingCleanup();
    static bool IsFooterErrorStatus(const std::string& status, bool isRunning);
    void DrawFooterCloseIcon(Rectangle bounds, bool hovered) const;
    void DrawFooterCopyIcon(Rectangle bounds, bool hovered) const;
    void DrawOverwritePrompt(int windowWidth, int windowHeight, Font font) const;
    void DrawAboutDialog(int windowWidth, int windowHeight, Font font) const;
    void DrawInfoDialog(int windowWidth, int windowHeight, Font font) const;
    static std::string GetDefaultDownloadPath();
    LinkCardNode* GetSelectedCard();
    const LinkCardNode* GetSelectedCard() const;
    LinkCardGroupNode* GetSelectedGroupHeader();
    const LinkCardGroupNode* GetSelectedGroupHeader() const;
    void PrepareGroupsForBatchDownload();
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
    Button seedTestLinksButton_{"+10"};
    Button seed8kLinkButton_{"8K"};
    Button downloadButton_{"Download Selected"};
    Button downloadAllButton_{"Download All"};
    Button convertButton_{"Convert Selected"};
    Button convertAllButton_{"Convert All"};
    Button cancelDownloadButton_{"Cancel"};
    Button cancelAllActionButton_{"Cancel All"};
    Button replaceFileButton_{"Replace"};
    Button cancelReplaceButton_{"Cancel"};
    Button cancelAllReplaceButton_{"Cancel All"};
    Button closeAboutButton_{"OK"};
    Button closeInfoButton_{"OK"};
    Dropdown fileFormatDropdown_{{"MP4"}};
    Dropdown mediaModeDropdown_{{"Both", "Video only", "Audio only"}};
    Dropdown qualityDropdown_{{"2160p"}};
    Dropdown convertContainerDropdown_{{"MP4", "MKV", "MOV", "WEBM"}};
    Dropdown convertVideoDropdown_{{"H.264", "H.265", "AV1", "VP9"}};
    Dropdown convertAudioDropdown_{{"AAC", "MP3", "Opus", "FLAC"}};
    Dropdown autoConvertContainerDropdown_{{"MP4", "MKV", "MOV", "WEBM"}};
    Dropdown autoConvertVideoDropdown_{{"H.264", "H.265", "AV1", "VP9"}};
    Dropdown autoConvertAudioDropdown_{{"AAC", "MP3", "Opus", "FLAC"}};
    Dropdown customAutoConvertContainerDropdown_{{"MP4", "MKV", "MOV", "WEBM"}};
    Dropdown customAutoConvertVideoDropdown_{{"H.264", "H.265", "AV1", "VP9"}};
    Dropdown customAutoConvertAudioDropdown_{{"AAC", "MP3", "Opus", "FLAC"}};
    Checkbox customPathCheckbox_;
    Checkbox keepIndicesCheckbox_;
    Dropdown cardConvertContainerDropdown_{{"MP4", "MKV", "MOV", "WEBM"}};
    Dropdown cardConvertVideoDropdown_{{"H.264", "H.265", "AV1", "VP9"}};
    Dropdown cardConvertAudioDropdown_{{"AAC", "MP3", "Opus", "FLAC"}};
    Checkbox convertContainerCheckbox_;
    Checkbox convertVideoCheckbox_;
    Checkbox convertAudioCheckbox_;
    CheckboxPaintSession converterGlobalCodecPaint_;
    Checkbox converterUseDefaultCheckbox_;
    Checkbox cardConvertContainerCheckbox_;
    Checkbox cardConvertVideoCheckbox_;
    Checkbox cardConvertAudioCheckbox_;
    CheckboxPaintSession converterCustomCodecPaint_;
    Checkbox autoConvertEnabledCheckbox_;
    Checkbox autoConvertContainerCheckbox_;
    Checkbox autoConvertVideoCheckbox_;
    Checkbox autoConvertAudioCheckbox_;
    CheckboxPaintSession autoConvertGlobalCodecPaint_;
    Checkbox autoConvertExcludeCheckbox_;
    Checkbox customAutoConvertEnabledCheckbox_;
    Checkbox customAutoConvertContainerCheckbox_;
    Checkbox customAutoConvertVideoCheckbox_;
    Checkbox customAutoConvertAudioCheckbox_;
    CheckboxPaintSession autoConvertCustomCodecPaint_;
    PathField customPathField_;
    PathField globalPathField_;
    AutoConvertOptions globalAutoConvert_;
    AutoConvertOptions customAutoConvert_;
    std::string customAutoConvertSelectionKey_;
    std::array<DownloadRunner, kMaxParallelDownloads> downloadRunners_;
    std::array<ConvertRunner, kMaxParallelConverts> convertRunners_;
    std::vector<ConverterFileCardNode> converterCards_;
    std::vector<DownloaderListItem> cards_;
    mutable std::vector<float> downloaderPrefixHeights_;
    mutable float downloaderCachedContentHeight_ = -1.0f;
    UndoStack undoStack_;
    bool suppressUndoRecording_ = false;
    bool suppressCardRemovalUndo_ = false;
    std::vector<LinkCardUndoSnapshot> pendingBatchLinkRemove_;
    std::vector<ConverterCardUndoSnapshot> pendingBatchConverterRemove_;
    std::string globalDownloadPath_;
    bool keepDownloadIndices_ = false;
    std::string lastChooseFileDirectory_;
    std::vector<DownloadRequest> pendingDownloadQueue_;
    std::unordered_set<std::string> softPreemptRequeueUrls_;
    std::vector<ConvertRequest> pendingConvertQueue_;
    std::vector<std::string> pendingStagingCleanupPaths_;
    double nextStagingCleanupTime_ = 0.0;
    DownloadRequest pendingOverwriteRequest_;
    ConvertRequest pendingOverwriteConvertRequest_;
    std::string pendingOverwriteFileName_;
    double nextDownloadStartTime_ = 0.0;
    float downloaderScrollOffset_ = 0.0f;
    float converterScrollOffset_ = 0.0f;
    float optionsScrollOffset_ = 0.0f;
    float converterOptionsScrollOffset_ = 0.0f;
    float infoDialogScrollOffset_ = 0.0f;
    Workspace activeWorkspace_ = Workspace::Downloader;
    bool isOverwritePromptOpen_ = false;
    bool isAboutDialogOpen_ = false;
    bool isInfoDialogOpen_ = false;
    bool overwritePromptIsConvert_ = false;
    int overwritePromptFocusIndex_ = 0;
    bool isBatchDownloading_ = false;
    bool isBatchConverting_ = false;
    double batchConvertElapsedTotal_ = 0.0;
    bool batchIncludesDownloadConvert_ = false;
    std::string lastConverterDropdownCardPath_;
    std::string converterCardOptionsSelectionKey_;
    ConverterOptions converterCardOptionsEdit_;
    bool converterCardOptionsUseDefault_ = true;
    bool converterCardOptionsUseDefaultMixed_ = false;
    bool converterCardOptionsCustomMixed_ = false;
    int lastConverterSelectionAnchor_ = -1;
    int lastDownloaderSelectionAnchor_ = -1;
    int lastConverterSelectionFocus_ = -1;
    int lastDownloaderSelectionFocus_ = -1;
    bool overwriteAllExisting_ = false;
    bool overlayBlocksActions_ = false;
    mutable int displayFps_ = 0;
    mutable int fpsFrameCounter_ = 0;
    mutable float fpsElapsedSeconds_ = 0.0f;
    bool convertContainer_ = false;
    bool convertVideo_ = false;
    bool convertAudio_ = false;
    FoldoutPanel downloadFoldout_{"Download", true};
    FoldoutPanel downloadResultSection_{"Download Result:", true, false, false};
    FoldoutPanel autoConvertSectionFoldout_{"Auto Convert", true};
    FoldoutPanel autoConvertFoldout_{"Global Convert", false, true};
    FoldoutPanel customAutoConvertFoldout_{"Custom Convert", false, true};
    FoldoutPanel converterSectionFoldout_{"Convert Options", true};
    FoldoutPanel converterDefaultFoldout_{"Global Options", true};
    FoldoutPanel converterCustomFoldout_{"Custom Options", false, true};
    FoldoutPanel converterResultSection_{"Converter Result:", true, false, false};
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
    static constexpr float kGroupCollapsedExtra = 8.0f;
    static constexpr float kHeaderHeight = 25.0f;
    static constexpr float kFooterHeight = 25.0f;
    static constexpr float kFooterNotificationMargin = 1.0f;
    static constexpr double kFooterNotificationDelaySeconds = 0.15;
    static constexpr double kFooterNotificationAutoHideSeconds = 10.0;
};

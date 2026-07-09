#pragma once

#include "ConverterInfoLoader.h"
#include "DownloadOptions.h"
#include "LinkInfoLoader.h"
#include "LinkGroupInfoLoader.h"

#include <memory>
#include <string>
#include <vector>

class DockArea;

struct LinkCardUndoSnapshot
{
    int index = 0;
    LinkInfo info;
    DownloadOptions options;
    std::string lastDownloadedPath;
    bool selected = false;
    bool excludeFromAutoConvert = false;
    AutoConvertOptions customAutoConvert;
};

struct ConverterCardUndoSnapshot
{
    int index = 0;
    ConverterFileInfo info;
    bool selected = false;
    bool useDefaultConvertSettings = true;
    ConverterOptions customOptions;
};

struct ConverterCardOptionsSnapshot
{
    std::string filePath;
    bool useDefaultConvertSettings = true;
    ConverterOptions customOptions;

    bool operator==(const ConverterCardOptionsSnapshot& other) const
    {
        return filePath == other.filePath && useDefaultConvertSettings == other.useDefaultConvertSettings &&
               customOptions == other.customOptions;
    }

    bool operator!=(const ConverterCardOptionsSnapshot& other) const
    {
        return !(*this == other);
    }
};

struct ConverterSettingsSnapshot
{
    bool convertContainer = false;
    bool convertVideo = false;
    bool convertAudio = false;
    int convertContainerIndex = 0;
    int convertVideoIndex = 0;
    int convertAudioIndex = 0;

    bool operator==(const ConverterSettingsSnapshot& other) const
    {
        return convertContainer == other.convertContainer && convertVideo == other.convertVideo &&
               convertAudio == other.convertAudio && convertContainerIndex == other.convertContainerIndex &&
               convertVideoIndex == other.convertVideoIndex && convertAudioIndex == other.convertAudioIndex;
    }

    bool operator!=(const ConverterSettingsSnapshot& other) const
    {
        return !(*this == other);
    }
};

struct LinkGroupChildUndoSnapshot
{
    std::string groupUrl;
    std::string childUrl;
    size_t entryIndex = 0;
    LinkGroupEntry entry;

    LinkInfo info;
    DownloadOptions options;
    std::string lastDownloadedPath;
    bool selected = false;
    bool groupHeaderSelected = false;
    bool excludeFromAutoConvert = false;
    AutoConvertOptions customAutoConvert;
};

class UndoCommand
{
public:
    virtual ~UndoCommand() = default;
    virtual void Undo(DockArea& dock) = 0;
    virtual void Redo(DockArea& dock) = 0;
};

class UndoStack
{
public:
    void Push(std::unique_ptr<UndoCommand> command);
    void Undo(DockArea& dock);
    void Redo(DockArea& dock);
    bool CanUndo() const;
    bool CanRedo() const;
    void Clear();

private:
    static constexpr size_t kMaxDepth = 50;
    std::vector<std::unique_ptr<UndoCommand>> undo_;
    std::vector<std::unique_ptr<UndoCommand>> redo_;
};

struct LinkExcludeFlag
{
    std::string url;
    bool excluded = false;
};

struct LinkCustomAutoConvertSnapshot
{
    std::string url;
    AutoConvertOptions options;
};

std::unique_ptr<UndoCommand> MakeRemoveLinkCardCommand(LinkCardUndoSnapshot snapshot);
std::unique_ptr<UndoCommand> MakeRemoveLinkCardsBatchCommand(std::vector<LinkCardUndoSnapshot> snapshots);
std::unique_ptr<UndoCommand> MakeRemoveConverterCardCommand(ConverterCardUndoSnapshot snapshot);
std::unique_ptr<UndoCommand> MakeRemoveConverterCardsBatchCommand(std::vector<ConverterCardUndoSnapshot> snapshots);
std::unique_ptr<UndoCommand> MakeCardOptionsCommand(std::string url, DownloadOptions before, DownloadOptions after);
std::unique_ptr<UndoCommand> MakeConverterSettingsCommand(ConverterSettingsSnapshot before,
                                                          ConverterSettingsSnapshot after);
std::unique_ptr<UndoCommand> MakeConverterCardOptionsCommand(std::string filePath,
                                                             ConverterCardOptionsSnapshot before,
                                                             ConverterCardOptionsSnapshot after);
std::unique_ptr<UndoCommand> MakeConverterCardOptionsBatchCommand(std::vector<ConverterCardOptionsSnapshot> before,
                                                                  std::vector<ConverterCardOptionsSnapshot> after);
std::unique_ptr<UndoCommand> MakeGlobalPathCommand(std::string before, std::string after);
std::unique_ptr<UndoCommand> MakeGlobalAutoConvertCommand(AutoConvertOptions before, AutoConvertOptions after);
std::unique_ptr<UndoCommand> MakeLinkCustomAutoConvertCommand(std::vector<LinkCustomAutoConvertSnapshot> before,
                                                              AutoConvertOptions after);
std::unique_ptr<UndoCommand> MakeLinkExcludeAutoConvertCommand(std::vector<LinkExcludeFlag> beforeFlags,
                                                               bool afterExcluded);
std::unique_ptr<UndoCommand> MakeDownloadAllCommand();
std::unique_ptr<UndoCommand> MakeCancelAllDownloadsCommand();
std::unique_ptr<UndoCommand> MakeRemoveLinkGroupChildCommand(LinkGroupChildUndoSnapshot snapshot);
std::unique_ptr<UndoCommand> MakeConvertAllCommand();
std::unique_ptr<UndoCommand> MakeCancelAllConvertsCommand();

bool operator==(const AutoConvertOptions& a, const AutoConvertOptions& b);
bool operator!=(const AutoConvertOptions& a, const AutoConvertOptions& b);
bool operator==(const DownloadOptions& a, const DownloadOptions& b);
bool operator!=(const DownloadOptions& a, const DownloadOptions& b);

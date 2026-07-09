#include "UndoStack.h"

#include "DockArea.h"

#include <algorithm>
#include <utility>

bool operator==(const AutoConvertOptions& a, const AutoConvertOptions& b)
{
    return a.enabled == b.enabled && a.convertContainer == b.convertContainer && a.convertVideo == b.convertVideo &&
           a.convertAudio == b.convertAudio && a.containerIndex == b.containerIndex && a.videoIndex == b.videoIndex &&
           a.audioIndex == b.audioIndex;
}

bool operator!=(const AutoConvertOptions& a, const AutoConvertOptions& b)
{
    return !(a == b);
}

bool operator==(const DownloadOptions& a, const DownloadOptions& b)
{
    return a.fileFormat == b.fileFormat && a.mediaMode == b.mediaMode && a.quality == b.quality &&
           a.qualityCap == b.qualityCap && a.useCustomPath == b.useCustomPath && a.customPath == b.customPath;
}

bool operator!=(const DownloadOptions& a, const DownloadOptions& b)
{
    return !(a == b);
}

void UndoStack::Push(std::unique_ptr<UndoCommand> command)
{
    if (command == nullptr)
    {
        return;
    }

    undo_.push_back(std::move(command));
    if (undo_.size() > kMaxDepth)
    {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
}

void UndoStack::Undo(DockArea& dock)
{
    if (undo_.empty())
    {
        return;
    }

    std::unique_ptr<UndoCommand> command = std::move(undo_.back());
    undo_.pop_back();
    command->Undo(dock);
    redo_.push_back(std::move(command));
}

void UndoStack::Redo(DockArea& dock)
{
    if (redo_.empty())
    {
        return;
    }

    std::unique_ptr<UndoCommand> command = std::move(redo_.back());
    redo_.pop_back();
    command->Redo(dock);
    undo_.push_back(std::move(command));
}

bool UndoStack::CanUndo() const
{
    return !undo_.empty();
}

bool UndoStack::CanRedo() const
{
    return !redo_.empty();
}

void UndoStack::Clear()
{
    undo_.clear();
    redo_.clear();
}

namespace
{

class RemoveLinkCardCommand final : public UndoCommand
{
public:
    explicit RemoveLinkCardCommand(LinkCardUndoSnapshot snapshot)
        : snapshot_(std::move(snapshot))
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoRestoreLinkCard(snapshot_);
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoRemoveLinkCardByUrl(snapshot_.info.url);
    }

private:
    LinkCardUndoSnapshot snapshot_;
};

class RemoveLinkCardsBatchCommand final : public UndoCommand
{
public:
    explicit RemoveLinkCardsBatchCommand(std::vector<LinkCardUndoSnapshot> snapshots)
        : snapshots_(std::move(snapshots))
    {
        std::sort(snapshots_.begin(),
                  snapshots_.end(),
                  [](const LinkCardUndoSnapshot& a, const LinkCardUndoSnapshot& b)
                  {
                      return a.index < b.index;
                  });
    }

    void Undo(DockArea& dock) override
    {
        for (const LinkCardUndoSnapshot& snapshot : snapshots_)
        {
            dock.UndoRestoreLinkCard(snapshot);
        }
    }

    void Redo(DockArea& dock) override
    {
        for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it)
        {
            dock.UndoRemoveLinkCardByUrl(it->info.url);
        }
    }

private:
    std::vector<LinkCardUndoSnapshot> snapshots_;
};

class RemoveConverterCardCommand final : public UndoCommand
{
public:
    explicit RemoveConverterCardCommand(ConverterCardUndoSnapshot snapshot)
        : snapshot_(std::move(snapshot))
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoRestoreConverterCard(snapshot_);
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoRemoveConverterCardByPath(snapshot_.info.filePath);
    }

private:
    ConverterCardUndoSnapshot snapshot_;
};

class RemoveConverterCardsBatchCommand final : public UndoCommand
{
public:
    explicit RemoveConverterCardsBatchCommand(std::vector<ConverterCardUndoSnapshot> snapshots)
        : snapshots_(std::move(snapshots))
    {
        std::sort(snapshots_.begin(),
                  snapshots_.end(),
                  [](const ConverterCardUndoSnapshot& a, const ConverterCardUndoSnapshot& b)
                  {
                      return a.index < b.index;
                  });
    }

    void Undo(DockArea& dock) override
    {
        for (const ConverterCardUndoSnapshot& snapshot : snapshots_)
        {
            dock.UndoRestoreConverterCard(snapshot);
        }
    }

    void Redo(DockArea& dock) override
    {
        for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it)
        {
            dock.UndoRemoveConverterCardByPath(it->info.filePath);
        }
    }

private:
    std::vector<ConverterCardUndoSnapshot> snapshots_;
};

class DownloadAllCommand final : public UndoCommand
{
public:
    void Undo(DockArea& dock) override
    {
        dock.UndoInvokeCancelAllDownloads();
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoInvokeDownloadAll();
    }
};

class CancelAllDownloadsCommand final : public UndoCommand
{
public:
    void Undo(DockArea& dock) override
    {
        dock.UndoInvokeDownloadAll();
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoInvokeCancelAllDownloads();
    }
};

class ConvertAllCommand final : public UndoCommand
{
public:
    void Undo(DockArea& dock) override
    {
        dock.UndoInvokeCancelAllConverts();
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoInvokeConvertAll();
    }
};

class CancelAllConvertsCommand final : public UndoCommand
{
public:
    void Undo(DockArea& dock) override
    {
        dock.UndoInvokeConvertAll();
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoInvokeCancelAllConverts();
    }
};

class CardOptionsCommand final : public UndoCommand
{
public:
    CardOptionsCommand(std::string url, DownloadOptions before, DownloadOptions after)
        : url_(std::move(url)),
          before_(std::move(before)),
          after_(std::move(after))
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoApplyCardOptions(url_, before_);
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoApplyCardOptions(url_, after_);
    }

private:
    std::string url_;
    DownloadOptions before_;
    DownloadOptions after_;
};

class ConverterSettingsCommand final : public UndoCommand
{
public:
    ConverterSettingsCommand(ConverterSettingsSnapshot before, ConverterSettingsSnapshot after)
        : before_(before),
          after_(after)
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoApplyConverterSettings(before_);
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoApplyConverterSettings(after_);
    }

private:
    ConverterSettingsSnapshot before_;
    ConverterSettingsSnapshot after_;
};

class ConverterCardOptionsCommand final : public UndoCommand
{
public:
    ConverterCardOptionsCommand(std::string filePath,
                                ConverterCardOptionsSnapshot before,
                                ConverterCardOptionsSnapshot after)
        : filePath_(std::move(filePath)),
          before_(std::move(before)),
          after_(std::move(after))
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoApplyConverterCardOptions(filePath_, before_);
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoApplyConverterCardOptions(filePath_, after_);
    }

private:
    std::string filePath_;
    ConverterCardOptionsSnapshot before_;
    ConverterCardOptionsSnapshot after_;
};

class ConverterCardOptionsBatchCommand final : public UndoCommand
{
public:
    ConverterCardOptionsBatchCommand(std::vector<ConverterCardOptionsSnapshot> before,
                                     std::vector<ConverterCardOptionsSnapshot> after)
        : before_(std::move(before)),
          after_(std::move(after))
    {
    }

    void Undo(DockArea& dock) override
    {
        for (const ConverterCardOptionsSnapshot& snapshot : before_)
        {
            dock.UndoApplyConverterCardOptions(snapshot.filePath, snapshot);
        }
    }

    void Redo(DockArea& dock) override
    {
        for (const ConverterCardOptionsSnapshot& snapshot : after_)
        {
            dock.UndoApplyConverterCardOptions(snapshot.filePath, snapshot);
        }
    }

private:
    std::vector<ConverterCardOptionsSnapshot> before_;
    std::vector<ConverterCardOptionsSnapshot> after_;
};

class GlobalPathCommand final : public UndoCommand
{
public:
    GlobalPathCommand(std::string before, std::string after)
        : before_(std::move(before)),
          after_(std::move(after))
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoApplyGlobalPath(before_);
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoApplyGlobalPath(after_);
    }

private:
    std::string before_;
    std::string after_;
};

class RemoveLinkGroupChildCommand final : public UndoCommand
{
public:
    explicit RemoveLinkGroupChildCommand(LinkGroupChildUndoSnapshot snapshot)
        : snapshot_(std::move(snapshot))
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoRestoreLinkGroupChild(snapshot_);
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoRemoveLinkGroupChild(snapshot_);
    }

private:
    LinkGroupChildUndoSnapshot snapshot_;
};

class GlobalAutoConvertCommand final : public UndoCommand
{
public:
    GlobalAutoConvertCommand(AutoConvertOptions before, AutoConvertOptions after)
        : before_(std::move(before)),
          after_(std::move(after))
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoApplyGlobalAutoConvert(before_);
    }

    void Redo(DockArea& dock) override
    {
        dock.UndoApplyGlobalAutoConvert(after_);
    }

private:
    AutoConvertOptions before_;
    AutoConvertOptions after_;
};

class LinkCustomAutoConvertCommand final : public UndoCommand
{
public:
    LinkCustomAutoConvertCommand(std::vector<LinkCustomAutoConvertSnapshot> before, AutoConvertOptions after)
        : before_(std::move(before)),
          after_(std::move(after))
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoApplyLinkCustomAutoConvert(before_);
    }

    void Redo(DockArea& dock) override
    {
        std::vector<LinkCustomAutoConvertSnapshot> afterSnapshots = before_;
        for (LinkCustomAutoConvertSnapshot& snapshot : afterSnapshots)
        {
            snapshot.options = after_;
        }
        dock.UndoApplyLinkCustomAutoConvert(afterSnapshots);
    }

private:
    std::vector<LinkCustomAutoConvertSnapshot> before_;
    AutoConvertOptions after_;
};

class LinkExcludeAutoConvertCommand final : public UndoCommand
{
public:
    LinkExcludeAutoConvertCommand(std::vector<LinkExcludeFlag> beforeFlags, bool afterExcluded)
        : beforeFlags_(std::move(beforeFlags)),
          afterExcluded_(afterExcluded)
    {
    }

    void Undo(DockArea& dock) override
    {
        dock.UndoApplyLinkExcludeFlags(beforeFlags_);
    }

    void Redo(DockArea& dock) override
    {
        std::vector<LinkExcludeFlag> afterFlags = beforeFlags_;
        for (LinkExcludeFlag& flag : afterFlags)
        {
            flag.excluded = afterExcluded_;
        }
        dock.UndoApplyLinkExcludeFlags(afterFlags);
    }

private:
    std::vector<LinkExcludeFlag> beforeFlags_;
    bool afterExcluded_ = false;
};

} // namespace

std::unique_ptr<UndoCommand> MakeRemoveLinkCardCommand(LinkCardUndoSnapshot snapshot)
{
    return std::make_unique<RemoveLinkCardCommand>(std::move(snapshot));
}

std::unique_ptr<UndoCommand> MakeRemoveLinkCardsBatchCommand(std::vector<LinkCardUndoSnapshot> snapshots)
{
    if (snapshots.empty())
    {
        return nullptr;
    }
    return std::make_unique<RemoveLinkCardsBatchCommand>(std::move(snapshots));
}

std::unique_ptr<UndoCommand> MakeRemoveConverterCardCommand(ConverterCardUndoSnapshot snapshot)
{
    return std::make_unique<RemoveConverterCardCommand>(std::move(snapshot));
}

std::unique_ptr<UndoCommand> MakeRemoveConverterCardsBatchCommand(std::vector<ConverterCardUndoSnapshot> snapshots)
{
    if (snapshots.empty())
    {
        return nullptr;
    }
    return std::make_unique<RemoveConverterCardsBatchCommand>(std::move(snapshots));
}

std::unique_ptr<UndoCommand> MakeDownloadAllCommand()
{
    return std::make_unique<DownloadAllCommand>();
}

std::unique_ptr<UndoCommand> MakeCancelAllDownloadsCommand()
{
    return std::make_unique<CancelAllDownloadsCommand>();
}

std::unique_ptr<UndoCommand> MakeRemoveLinkGroupChildCommand(LinkGroupChildUndoSnapshot snapshot)
{
    return std::make_unique<RemoveLinkGroupChildCommand>(std::move(snapshot));
}

std::unique_ptr<UndoCommand> MakeConvertAllCommand()
{
    return std::make_unique<ConvertAllCommand>();
}

std::unique_ptr<UndoCommand> MakeCancelAllConvertsCommand()
{
    return std::make_unique<CancelAllConvertsCommand>();
}

std::unique_ptr<UndoCommand> MakeCardOptionsCommand(std::string url, DownloadOptions before, DownloadOptions after)
{
    if (before == after)
    {
        return nullptr;
    }
    return std::make_unique<CardOptionsCommand>(std::move(url), std::move(before), std::move(after));
}

std::unique_ptr<UndoCommand> MakeConverterSettingsCommand(ConverterSettingsSnapshot before,
                                                          ConverterSettingsSnapshot after)
{
    if (before == after)
    {
        return nullptr;
    }
    return std::make_unique<ConverterSettingsCommand>(before, after);
}

std::unique_ptr<UndoCommand> MakeConverterCardOptionsCommand(std::string filePath,
                                                             ConverterCardOptionsSnapshot before,
                                                             ConverterCardOptionsSnapshot after)
{
    if (before == after)
    {
        return nullptr;
    }
    return std::make_unique<ConverterCardOptionsCommand>(std::move(filePath), std::move(before), std::move(after));
}

std::unique_ptr<UndoCommand> MakeConverterCardOptionsBatchCommand(std::vector<ConverterCardOptionsSnapshot> before,
                                                                  std::vector<ConverterCardOptionsSnapshot> after)
{
    if (before.size() != after.size() || before.empty())
    {
        return nullptr;
    }
    for (size_t index = 0; index < before.size(); ++index)
    {
        if (before[index] != after[index])
        {
            return std::make_unique<ConverterCardOptionsBatchCommand>(std::move(before), std::move(after));
        }
    }
    return nullptr;
}

std::unique_ptr<UndoCommand> MakeGlobalPathCommand(std::string before, std::string after)
{
    if (before == after)
    {
        return nullptr;
    }
    return std::make_unique<GlobalPathCommand>(std::move(before), std::move(after));
}

std::unique_ptr<UndoCommand> MakeGlobalAutoConvertCommand(AutoConvertOptions before, AutoConvertOptions after)
{
    if (before == after)
    {
        return nullptr;
    }
    return std::make_unique<GlobalAutoConvertCommand>(std::move(before), std::move(after));
}

std::unique_ptr<UndoCommand> MakeLinkCustomAutoConvertCommand(std::vector<LinkCustomAutoConvertSnapshot> before,
                                                              AutoConvertOptions after)
{
    if (before.empty())
    {
        return nullptr;
    }
    bool anyChange = false;
    for (const LinkCustomAutoConvertSnapshot& snapshot : before)
    {
        if (snapshot.options != after)
        {
            anyChange = true;
            break;
        }
    }
    if (!anyChange)
    {
        return nullptr;
    }
    return std::make_unique<LinkCustomAutoConvertCommand>(std::move(before), std::move(after));
}

std::unique_ptr<UndoCommand> MakeLinkExcludeAutoConvertCommand(std::vector<LinkExcludeFlag> beforeFlags,
                                                               bool afterExcluded)
{
    if (beforeFlags.empty())
    {
        return nullptr;
    }
    bool anyChange = false;
    for (const LinkExcludeFlag& flag : beforeFlags)
    {
        if (flag.excluded != afterExcluded)
        {
            anyChange = true;
            break;
        }
    }
    if (!anyChange)
    {
        return nullptr;
    }
    return std::make_unique<LinkExcludeAutoConvertCommand>(std::move(beforeFlags), afterExcluded);
}

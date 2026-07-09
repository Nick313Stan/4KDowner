#pragma once

#include <memory>
#include <string>

class LinkCardNode;
class LinkCardGroupNode;

struct DownloaderListItem
{
    enum class Kind
    {
        Single,
        Group,
    };

    Kind kind = Kind::Single;
    std::unique_ptr<LinkCardNode> single;
    std::unique_ptr<LinkCardGroupNode> group;

    DownloaderListItem();
    ~DownloaderListItem();
    DownloaderListItem(DownloaderListItem&&) noexcept;
    DownloaderListItem& operator=(DownloaderListItem&&) noexcept;
    DownloaderListItem(const DownloaderListItem&) = delete;
    DownloaderListItem& operator=(const DownloaderListItem&) = delete;

    static DownloaderListItem MakeSingle(std::string url);
    static DownloaderListItem MakeGroup(std::string url);
    static DownloaderListItem MakeFromUrl(std::string url);

    float Height() const;
    bool HasUrl(const std::string& url) const;
    void ClearSelection();
    bool AnySelected() const;
    bool IsGroupHeaderSelected() const;
    LinkCardNode* GetSelectedChild();
    const LinkCardNode* GetSelectedChild() const;
    int CountSelectableChildren() const;

    LinkCardNode* SingleOrNull();
    const LinkCardNode* SingleOrNull() const;
    LinkCardGroupNode* GroupOrNull();
    const LinkCardGroupNode* GroupOrNull() const;

    bool IsHovered() const;
    bool IsSelected() const;
    void SetSelected(bool selected);
};

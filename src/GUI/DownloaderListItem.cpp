#include "DownloaderListItemInclude.h"

#include "LinkCardGroupNodeInclude.h"
#include "LinkCardNode.h"
#include "LinkGroupInfoLoader.h"

DownloaderListItem::DownloaderListItem() = default;

DownloaderListItem::~DownloaderListItem() = default;

DownloaderListItem::DownloaderListItem(DownloaderListItem&&) noexcept = default;

DownloaderListItem& DownloaderListItem::operator=(DownloaderListItem&&) noexcept = default;

DownloaderListItem DownloaderListItem::MakeSingle(std::string url)
{
    DownloaderListItem item;
    item.kind = Kind::Single;
    item.single = std::make_unique<LinkCardNode>(std::move(url));
    return item;
}

DownloaderListItem DownloaderListItem::MakeGroup(std::string url)
{
    DownloaderListItem item;
    item.kind = Kind::Group;
    item.group = std::make_unique<LinkCardGroupNode>(std::move(url));
    return item;
}

DownloaderListItem DownloaderListItem::MakeFromUrl(std::string url)
{
    if (LooksLikeGroupUrl(url))
    {
        return MakeGroup(std::move(url));
    }
    return MakeSingle(std::move(url));
}

LinkCardNode* DownloaderListItem::SingleOrNull()
{
    return kind == Kind::Single ? single.get() : nullptr;
}

const LinkCardNode* DownloaderListItem::SingleOrNull() const
{
    return kind == Kind::Single ? single.get() : nullptr;
}

LinkCardGroupNode* DownloaderListItem::GroupOrNull()
{
    return kind == Kind::Group ? group.get() : nullptr;
}

const LinkCardGroupNode* DownloaderListItem::GroupOrNull() const
{
    return kind == Kind::Group ? group.get() : nullptr;
}

float DownloaderListItem::Height() const
{
    if (kind == Kind::Single)
    {
        return LinkCardGroupNode::kCardHeight;
    }
    return group != nullptr ? group->TotalHeight() : LinkCardGroupNode::kCardHeight;
}

bool DownloaderListItem::HasUrl(const std::string& url) const
{
    if (kind == Kind::Single)
    {
        return single != nullptr && single->HasUrl(url);
    }
    return group != nullptr && group->HasUrl(url);
}

void DownloaderListItem::ClearSelection()
{
    if (kind == Kind::Single)
    {
        if (single != nullptr)
        {
            single->SetSelected(false);
        }
        return;
    }
    if (group != nullptr)
    {
        group->SetHeaderSelected(false);
        group->ClearChannelTabSelection();
        group->ClearPlaylistsTabSelection();
        group->ClearPlaylistShelfSelection();
        group->ClearLoadedCardSelection();
    }
}

bool DownloaderListItem::AnySelected() const
{
    if (kind == Kind::Single)
    {
        return single != nullptr && single->IsSelected();
    }
    if (group == nullptr)
    {
        return false;
    }
    if (group->IsHeaderSelected() || group->AnyChannelTabSelected() || group->SelectedPlaylistShelfIndex() >= 0)
    {
        return true;
    }
    bool found = false;
    static_cast<const LinkCardGroupNode&>(*group).ForEachLoadedCard(
        [&](const LinkCardNode& child)
        {
            if (child.IsSelected())
            {
                found = true;
            }
        });
    return found;
}

bool DownloaderListItem::IsGroupHeaderSelected() const
{
    return kind == Kind::Group && group != nullptr && group->IsHeaderSelected();
}

LinkCardNode* DownloaderListItem::GetSelectedChild()
{
    if (kind != Kind::Group || group == nullptr)
    {
        return nullptr;
    }
    LinkCardNode* found = nullptr;
    group->ForEachLoadedCard(
        [&](LinkCardNode& child)
        {
            if (found == nullptr && child.IsSelected())
            {
                found = &child;
            }
        });
    return found;
}

const LinkCardNode* DownloaderListItem::GetSelectedChild() const
{
    if (kind != Kind::Group || group == nullptr)
    {
        return nullptr;
    }
    const LinkCardNode* found = nullptr;
    static_cast<const LinkCardGroupNode&>(*group).ForEachLoadedCard(
        [&](const LinkCardNode& child)
        {
            if (found == nullptr && child.IsSelected())
            {
                found = &child;
            }
        });
    return found;
}

int DownloaderListItem::CountSelectableChildren() const
{
    if (kind != Kind::Group || group == nullptr)
    {
        return 0;
    }
    return group->LoadedChildCount();
}

bool DownloaderListItem::IsHovered() const
{
    if (kind == Kind::Single)
    {
        return single != nullptr && single->IsHovered();
    }
    if (group == nullptr)
    {
        return false;
    }
    if (group->IsHeaderHovered())
    {
        return true;
    }
    for (int tab = 0; tab < kChannelTabCount; ++tab)
    {
        if (group->IsChannelTabHovered(tab))
        {
            return true;
        }
    }
    if (group->UsesPlaylistShelf())
    {
        if (group->IsPlaylistsTabHovered())
        {
            return true;
        }
        for (int shelfIndex = 0; shelfIndex < group->PlaylistShelfLoadedCount(); ++shelfIndex)
        {
            if (group->IsPlaylistShelfItemHovered(shelfIndex))
            {
                return true;
            }
        }
    }
    bool hovered = false;
    static_cast<const LinkCardGroupNode&>(*group).ForEachLoadedCard(
        [&](const LinkCardNode& child)
        {
            if (child.IsHovered())
            {
                hovered = true;
            }
        });
    return hovered;
}

bool DownloaderListItem::IsSelected() const
{
    return AnySelected();
}

void DownloaderListItem::SetSelected(bool selected)
{
    if (kind == Kind::Single)
    {
        if (single != nullptr)
        {
            single->SetSelected(selected);
        }
        return;
    }
    if (group == nullptr)
    {
        return;
    }
    group->SetHeaderSelected(selected);
    if (!selected)
    {
        group->ClearChannelTabSelection();
        group->ClearPlaylistShelfSelection();
        group->ClearLoadedCardSelection();
    }
    else
    {
        group->ClearPlaylistShelfSelection();
        group->ClearLoadedCardSelection();
    }
}

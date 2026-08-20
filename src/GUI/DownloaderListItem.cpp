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
        for (LinkCardNode& child : group->LoadedCards())
        {
            child.SetSelected(false);
        }
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
    if (group->IsHeaderSelected())
    {
        return true;
    }
    for (const LinkCardNode& child : group->LoadedCards())
    {
        if (child.IsSelected())
        {
            return true;
        }
    }
    return false;
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
    for (LinkCardNode& child : group->LoadedCards())
    {
        if (child.IsSelected())
        {
            return &child;
        }
    }
    return nullptr;
}

const LinkCardNode* DownloaderListItem::GetSelectedChild() const
{
    if (kind != Kind::Group || group == nullptr)
    {
        return nullptr;
    }
    for (const LinkCardNode& child : group->LoadedCards())
    {
        if (child.IsSelected())
        {
            return &child;
        }
    }
    return nullptr;
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
    for (const LinkCardNode& child : group->LoadedCards())
    {
        if (child.IsHovered())
        {
            return true;
        }
    }
    return false;
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
        for (LinkCardNode& child : group->LoadedCards())
        {
            child.SetSelected(false);
        }
    }
    else
    {
        for (LinkCardNode& child : group->LoadedCards())
        {
            child.SetSelected(false);
        }
    }
}

#include "EmojiText.h"

#include "SpriteEmojiBackend.h"
#include "WinAppPaths.h"

#include <filesystem>

namespace
{
class NullEmojiBackend final : public IEmojiBackend
{
public:
    bool Supports(std::u32string_view) const override
    {
        return false;
    }

    std::optional<EmojiSprite> GetOrLoad(const EmojiKey&) override
    {
        return std::nullopt;
    }

    void Pump() override {}

    void UnloadAll() override {}
};

std::filesystem::path DefaultEmojiCacheRoot()
{
    return GetDocuments4KDownerTempPath() / "cache" / "emoji";
}

std::filesystem::path DefaultEmojiBundledRoot()
{
    return FindAssetPath(std::filesystem::path("assets") / "emoji");
}

std::unique_ptr<IEmojiBackend> MakeSpriteBackend(SpriteEmojiPack pack)
{
    return std::make_unique<SpriteEmojiBackend>(DefaultEmojiBundledRoot(), DefaultEmojiCacheRoot(), std::move(pack));
}
} // namespace

std::unique_ptr<IEmojiBackend> CreateEmojiBackend(EmojiBackendKind kind)
{
    switch (kind)
    {
    case EmojiBackendKind::NotoSprites:
        return MakeSpriteBackend(SpriteEmojiBackend::NotoPack());
    case EmojiBackendKind::Sprites:
        return MakeSpriteBackend(SpriteEmojiBackend::TwemojiPack());
    case EmojiBackendKind::Null:
    default:
        return std::make_unique<NullEmojiBackend>();
    }
}

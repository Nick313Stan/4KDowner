#pragma once

#include "raylib.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct EmojiKey
{
    std::u32string sequence;
    int pixelSize = 0;

    bool operator==(const EmojiKey& other) const
    {
        return pixelSize == other.pixelSize && sequence == other.sequence;
    }
};

struct EmojiSprite
{
    Texture2D texture{};
    Rectangle src{};
    float advance = 0.0f;
    float ascent = 0.0f;
};

class IEmojiBackend
{
public:
    virtual ~IEmojiBackend() = default;

    virtual bool Supports(std::u32string_view sequence) const = 0;
    // May return empty while a download is in flight; never blocks the UI thread on network.
    virtual std::optional<EmojiSprite> GetOrLoad(const EmojiKey& key) = 0;
    virtual void Pump() = 0;
    virtual void UnloadAll() = 0;
};

enum class EmojiBackendKind
{
    Null,
    Sprites,     // Twemoji PNG pack
    NotoSprites, // Noto-like PNG pack (default)
};

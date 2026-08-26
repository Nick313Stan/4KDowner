#include "EmojiText.h"

#include <cctype>
#include <sstream>

namespace
{
IEmojiBackend* g_emojiBackend = nullptr;

bool IsEmojiCodepoint(char32_t cp)
{
    if (cp == 0x200D || cp == 0xFE0F || cp == 0x20E3)
    {
        return true;
    }
    if (cp >= 0x1F3FB && cp <= 0x1F3FF)
    {
        return true;
    }
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF)
    {
        return true;
    }
    if (cp >= 0x2600 && cp <= 0x27BF)
    {
        return true;
    }
    if (cp >= 0x1F300 && cp <= 0x1FAFF)
    {
        return true;
    }
    if (cp >= 0x1F900 && cp <= 0x1F9FF)
    {
        return true;
    }
    return false;
}

bool IsEmojiStarter(char32_t cp)
{
    if (cp == 0x200D || cp == 0xFE0F || cp == 0x20E3)
    {
        return false;
    }
    return IsEmojiCodepoint(cp);
}

bool DecodeUtf8(const std::string& utf8, size_t& index, char32_t& out)
{
    if (index >= utf8.size())
    {
        return false;
    }
    const unsigned char c0 = static_cast<unsigned char>(utf8[index]);
    if (c0 < 0x80)
    {
        out = c0;
        ++index;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0 && index + 1 < utf8.size())
    {
        out = (static_cast<char32_t>(c0 & 0x1F) << 6) |
              (static_cast<char32_t>(static_cast<unsigned char>(utf8[index + 1]) & 0x3F));
        index += 2;
        return true;
    }
    if ((c0 & 0xF0) == 0xE0 && index + 2 < utf8.size())
    {
        out = (static_cast<char32_t>(c0 & 0x0F) << 12) |
              (static_cast<char32_t>(static_cast<unsigned char>(utf8[index + 1]) & 0x3F) << 6) |
              (static_cast<char32_t>(static_cast<unsigned char>(utf8[index + 2]) & 0x3F));
        index += 3;
        return true;
    }
    if ((c0 & 0xF8) == 0xF0 && index + 3 < utf8.size())
    {
        out = (static_cast<char32_t>(c0 & 0x07) << 18) |
              (static_cast<char32_t>(static_cast<unsigned char>(utf8[index + 1]) & 0x3F) << 12) |
              (static_cast<char32_t>(static_cast<unsigned char>(utf8[index + 2]) & 0x3F) << 6) |
              (static_cast<char32_t>(static_cast<unsigned char>(utf8[index + 3]) & 0x3F));
        index += 4;
        return true;
    }
    out = c0;
    ++index;
    return true;
}

void AppendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

float EmojiAdvance(IEmojiBackend* backend, const std::u32string& sequence, float fontSize)
{
    if (backend != nullptr)
    {
        const EmojiKey key{sequence, static_cast<int>(fontSize + 0.5f)};
        if (const std::optional<EmojiSprite> sprite = backend->GetOrLoad(key))
        {
            return sprite->advance > 0.0f ? sprite->advance : fontSize;
        }
    }
    // Reserve layout space so wrap stays stable while the sprite loads.
    return fontSize;
}

float MeasureRuns(Font font, const std::vector<EmojiText::Run>& runs, float fontSize)
{
    float width = 0.0f;
    IEmojiBackend* backend = EmojiText::Backend();
    for (const EmojiText::Run& run : runs)
    {
        if (run.kind == EmojiText::RunKind::Text)
        {
            if (!run.utf8.empty())
            {
                width += MeasureTextEx(font, run.utf8.c_str(), fontSize, 0.0f).x;
            }
        }
        else
        {
            width += EmojiAdvance(backend, run.sequence, fontSize);
        }
    }
    return width;
}

void DrawRuns(Font font, const std::vector<EmojiText::Run>& runs, Vector2 position, float fontSize, Color color)
{
    float x = position.x;
    const float y = position.y;
    IEmojiBackend* backend = EmojiText::Backend();
    for (const EmojiText::Run& run : runs)
    {
        if (run.kind == EmojiText::RunKind::Text)
        {
            if (run.utf8.empty())
            {
                continue;
            }
            DrawTextEx(font, run.utf8.c_str(), {x, y}, fontSize, 0.0f, color);
            x += MeasureTextEx(font, run.utf8.c_str(), fontSize, 0.0f).x;
            continue;
        }

        const float advance = EmojiAdvance(backend, run.sequence, fontSize);
        if (backend != nullptr)
        {
            const EmojiKey key{run.sequence, static_cast<int>(fontSize + 0.5f)};
            if (const std::optional<EmojiSprite> sprite = backend->GetOrLoad(key))
            {
                const float drawSize = fontSize;
                const Rectangle dest{x, y + (fontSize - drawSize) * 0.1f, drawSize, drawSize};
                DrawTexturePro(sprite->texture, sprite->src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
            }
        }
        x += advance;
    }
}
} // namespace

namespace EmojiText
{
void SetBackend(IEmojiBackend* backend)
{
    g_emojiBackend = backend;
}

IEmojiBackend* Backend()
{
    return g_emojiBackend;
}

std::vector<Run> ParseRuns(const std::string& utf8)
{
    std::vector<Run> runs;
    std::string textBuf;
    size_t index = 0;
    while (index < utf8.size())
    {
        const size_t start = index;
        char32_t cp = 0;
        if (!DecodeUtf8(utf8, index, cp))
        {
            break;
        }
        (void)start;

        if (IsEmojiStarter(cp))
        {
            if (!textBuf.empty())
            {
                runs.push_back(Run{RunKind::Text, textBuf, {}});
                textBuf.clear();
            }

            std::u32string sequence;
            sequence.push_back(cp);
            for (;;)
            {
                size_t peek = index;
                char32_t next = 0;
                if (peek >= utf8.size() || !DecodeUtf8(utf8, peek, next))
                {
                    break;
                }

                if (next == 0xFE0F || next == 0x20E3 || (next >= 0x1F3FB && next <= 0x1F3FF))
                {
                    sequence.push_back(next);
                    index = peek;
                    continue;
                }
                if (next == 0x200D)
                {
                    sequence.push_back(next);
                    index = peek;
                    size_t afterZwj = index;
                    char32_t followed = 0;
                    if (afterZwj < utf8.size() && DecodeUtf8(utf8, afterZwj, followed) && IsEmojiStarter(followed))
                    {
                        sequence.push_back(followed);
                        index = afterZwj;
                        continue;
                    }
                    break;
                }
                if (next >= 0x1F1E6 && next <= 0x1F1FF && !sequence.empty() && sequence.back() >= 0x1F1E6 &&
                    sequence.back() <= 0x1F1FF)
                {
                    sequence.push_back(next);
                    index = peek;
                    continue;
                }
                break;
            }
            runs.push_back(Run{RunKind::Emoji, {}, std::move(sequence)});
            continue;
        }

        AppendUtf8(textBuf, cp);
    }

    if (!textBuf.empty())
    {
        runs.push_back(Run{RunKind::Text, textBuf, {}});
    }
    return runs;
}

float MeasureWidth(Font font, const std::string& text, float fontSize)
{
    return MeasureRuns(font, ParseRuns(text), fontSize);
}

std::vector<std::string> WrapLines(Font font, const std::string& text, float fontSize, float maxWidth, int maxLines)
{
    std::stringstream stream(text);
    std::vector<std::string> lines;
    std::string word;
    std::string currentLine;

    while (stream >> word)
    {
        const std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
        if (MeasureWidth(font, candidate, fontSize) <= maxWidth)
        {
            currentLine = candidate;
            continue;
        }

        if (!currentLine.empty())
        {
            lines.push_back(currentLine);
            currentLine = word;
        }
        else
        {
            lines.push_back(word);
            currentLine.clear();
        }

        if (static_cast<int>(lines.size()) >= maxLines)
        {
            break;
        }
    }

    if (!currentLine.empty() && static_cast<int>(lines.size()) < maxLines)
    {
        lines.push_back(currentLine);
    }

    return lines;
}

void Draw(Font font, const std::string& text, Vector2 position, float fontSize, Color color)
{
    DrawRuns(font, ParseRuns(text), position, fontSize, color);
}

void DrawWrapped(
    Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color)
{
    const std::vector<std::string> lines = WrapLines(font, text, fontSize, maxWidth, maxLines);
    for (int index = 0; index < static_cast<int>(lines.size()); ++index)
    {
        Draw(font,
             lines[static_cast<size_t>(index)],
             {position.x, position.y + static_cast<float>(index) * (fontSize + 3.0f)},
             fontSize,
             color);
    }
}

float MeasureWrappedHeight(Font font, const std::string& text, float fontSize, float maxWidth, int maxLines)
{
    const std::vector<std::string> lines = WrapLines(font, text, fontSize, maxWidth, maxLines);
    if (lines.empty())
    {
        return fontSize;
    }
    return static_cast<float>(lines.size()) * fontSize + static_cast<float>(lines.size() - 1) * 3.0f;
}

void Pump()
{
    if (g_emojiBackend != nullptr)
    {
        g_emojiBackend->Pump();
    }
}
} // namespace EmojiText

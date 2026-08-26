#pragma once

#include "IEmojiBackend.h"

#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace EmojiText
{
enum class RunKind
{
    Text,
    Emoji,
};

struct Run
{
    RunKind kind = RunKind::Text;
    std::string utf8;        // Text run (UTF-8) or empty for emoji
    std::u32string sequence; // Emoji cluster codepoints
};

void SetBackend(IEmojiBackend* backend);
IEmojiBackend* Backend();

std::vector<Run> ParseRuns(const std::string& utf8);

float MeasureWidth(Font font, const std::string& text, float fontSize);
std::vector<std::string> WrapLines(Font font, const std::string& text, float fontSize, float maxWidth, int maxLines);
void Draw(Font font, const std::string& text, Vector2 position, float fontSize, Color color);
void DrawWrapped(
    Font font, const std::string& text, Vector2 position, float fontSize, float maxWidth, int maxLines, Color color);
float MeasureWrappedHeight(Font font, const std::string& text, float fontSize, float maxWidth, int maxLines);

void Pump();
} // namespace EmojiText

std::unique_ptr<IEmojiBackend> CreateEmojiBackend(EmojiBackendKind kind);

#include "VideoTitle.h"

#include <cctype>

namespace
{
bool DecodeUtf8(const std::string& text, size_t& index, int& codepoint)
{
    if (index >= text.size())
    {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(text[index]);
    if (first < 0x80)
    {
        codepoint = first;
        ++index;
        return true;
    }

    int extraBytes = 0;
    if ((first & 0xE0) == 0xC0)
    {
        extraBytes = 1;
        codepoint = first & 0x1F;
    }
    else if ((first & 0xF0) == 0xE0)
    {
        extraBytes = 2;
        codepoint = first & 0x0F;
    }
    else if ((first & 0xF8) == 0xF0)
    {
        extraBytes = 3;
        codepoint = first & 0x07;
    }
    else
    {
        ++index;
        return false;
    }

    if (index + static_cast<size_t>(extraBytes) >= text.size())
    {
        index = text.size();
        return false;
    }

    for (int byteIndex = 0; byteIndex < extraBytes; ++byteIndex)
    {
        const unsigned char next = static_cast<unsigned char>(text[index + static_cast<size_t>(byteIndex) + 1]);
        if ((next & 0xC0) != 0x80)
        {
            index += static_cast<size_t>(byteIndex) + 1;
            return false;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
    }

    index += static_cast<size_t>(extraBytes) + 1;
    return true;
}

void AppendUtf8(int codepoint, std::string& output)
{
    if (codepoint <= 0x7F)
    {
        output.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0xFFFF)
    {
        output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0x10FFFF)
    {
        output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool IsForbiddenCodepoint(int codepoint)
{
    if (codepoint < 32)
    {
        return true;
    }

    switch (codepoint)
    {
    case '<':
    case '>':
    case ':':
    case '"':
    case '/':
    case '\\':
    case '|':
    case '?':
    case '*':
    case 0xFF5C: // ｜
    case 0xFF0F: // ／
    case 0xFF3C: // ＼
    case 0xFF1A: // ：
    case 0xFF1F: // ？
    case 0xFF0A: // ＊
    case 0x201C:
    case 0x201D:
    case 0x2018:
    case 0x2019:
    case 0x00AB:
    case 0x00BB:
        return true;
    default:
        return false;
    }
}

bool IsAllowedCodepoint(int codepoint)
{
    if (IsForbiddenCodepoint(codepoint))
    {
        return false;
    }

    if (codepoint == ' ' || codepoint == '-' || codepoint == '_' || codepoint == '\'' || codepoint == '.' ||
        codepoint == '(' || codepoint == ')' || codepoint == '+')
    {
        return true;
    }

    if (codepoint < 0x80)
    {
        return std::isalnum(codepoint) != 0;
    }

    if (codepoint >= 0x2000 && codepoint <= 0x206F)
    {
        return false;
    }
    if (codepoint >= 0x3000 && codepoint <= 0x303F)
    {
        return false;
    }
    if (codepoint >= 0xFF00 && codepoint <= 0xFFEF)
    {
        return false;
    }

    return true;
}

std::string CollapseSpaces(std::string value)
{
    std::string result;
    result.reserve(value.size());
    bool pendingSpace = false;
    for (const char c : value)
    {
        if (c == ' ')
        {
            pendingSpace = !result.empty();
            continue;
        }
        if (pendingSpace)
        {
            result.push_back(' ');
            pendingSpace = false;
        }
        result.push_back(c);
    }
    return result;
}

std::string TrimEdges(std::string value)
{
    while (!value.empty() && value.front() == ' ')
    {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '.'))
    {
        value.pop_back();
    }
    return value;
}
} // namespace

std::string NormalizeVideoTitle(const std::string& title)
{
    std::string normalized;
    normalized.reserve(title.size());

    for (size_t index = 0; index < title.size();)
    {
        int codepoint = 0;
        const size_t before = index;
        if (!DecodeUtf8(title, index, codepoint))
        {
            continue;
        }

        if (!IsAllowedCodepoint(codepoint))
        {
            continue;
        }

        if (codepoint == ' ')
        {
            normalized.push_back(' ');
            continue;
        }

        AppendUtf8(codepoint, normalized);
        (void)before;
    }

    normalized = TrimEdges(CollapseSpaces(std::move(normalized)));
    if (normalized.size() > 200)
    {
        normalized.resize(200);
        normalized = TrimEdges(std::move(normalized));
    }

    return normalized.empty() ? "video" : normalized;
}

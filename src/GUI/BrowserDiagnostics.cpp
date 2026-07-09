#include "BrowserDiagnostics.h"

#include "YtDlpYouTube.h"

#include <sstream>

namespace
{
std::string TrimForSummary(std::string value, size_t maxLength = 220)
{
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
    {
        value.pop_back();
    }

    if (value.size() <= maxLength)
    {
        return value;
    }

    return value.substr(0, maxLength) + "...";
}
}

void BrowserAttemptLog::Clear()
{
    attempts_.clear();
    winner_.clear();
    winnerSet_ = false;
}

void BrowserAttemptLog::AddAttempt(BrowserAttempt attempt)
{
    attempts_.push_back(std::move(attempt));
}

void BrowserAttemptLog::SetWinner(const std::string& browserSpec)
{
    winner_ = browserSpec;
    winnerSet_ = true;
}

const std::string& BrowserAttemptLog::Winner() const
{
    return winner_;
}

bool BrowserAttemptLog::HasWinner() const
{
    return winnerSet_;
}

const std::vector<BrowserAttempt>& BrowserAttemptLog::Attempts() const
{
    return attempts_;
}

bool BrowserAttemptLog::Empty() const
{
    return attempts_.empty();
}

std::string FormatBrowserAuthLabel(const std::string& browserSpec)
{
    if (browserSpec.empty())
    {
        return "no cookies";
    }

    return browserSpec + " (with cookies)";
}

std::string SummarizeBrowserAttemptOutput(const std::string& output, bool parseMode)
{
    if (output.empty())
    {
        return parseMode ? "No parser output captured." : "No yt-dlp output captured.";
    }

    return TrimForSummary(SimplifyYtDlpError(output));
}

std::string DescribeBrowserRetryAction(
    const std::string& output,
    bool hasMoreBrowsers,
    bool success)
{
    if (success)
    {
        return "Used for this operation.";
    }

    if (!hasMoreBrowsers)
    {
        return "No more browser options left to try.";
    }

    if (ShouldRetryYoutubeWithDifferentCookies(output))
    {
        return "Trying next browser option.";
    }

    return "Stopped retrying browsers for this error.";
}

std::string BrowserAttemptLog::FormatSection(const std::string& title) const
{
    std::ostringstream stream;
    stream << "--- " << title << " ---\n";

    if (attempts_.empty())
    {
        stream << "No browser attempts recorded.\n";
        return stream.str();
    }

    if (winnerSet_)
    {
        stream << "Winner: " << FormatBrowserAuthLabel(winner_) << "\n";
        stream << "Cookies: " << (winner_.empty() ? "no" : "yes") << "\n\n";
    }
    else
    {
        stream << "Winner: (none)\n\n";
    }

    for (size_t index = 0; index < attempts_.size(); ++index)
    {
        const BrowserAttempt& attempt = attempts_[index];
        stream << (index + 1) << ". " << FormatBrowserAuthLabel(attempt.browserSpec) << "\n";
        stream << "   Result: " << (attempt.success ? "SUCCESS" : "FAILED") << "\n";
        if (!attempt.summary.empty())
        {
            stream << "   Details: " << attempt.summary << "\n";
        }
        if (!attempt.nextAction.empty())
        {
            stream << "   Next: " << attempt.nextAction << "\n";
        }
        stream << "\n";
    }

    return stream.str();
}

std::string FormatBrowserSessionReport(
    const std::string& url,
    const std::string& title,
    const std::string& parseReport,
    const std::string& downloadReport)
{
    std::ostringstream stream;
    stream << "4KDowner browser report\n";
    if (!title.empty())
    {
        stream << "Title: " << title << "\n";
    }
    if (!url.empty())
    {
        stream << "URL: " << url << "\n";
    }
    stream << "\n";
    if (!parseReport.empty())
    {
        stream << parseReport;
    }
    else
    {
        stream << "--- Parse ---\nNo browser attempts recorded.\n";
    }
    stream << "\n";
    if (!downloadReport.empty())
    {
        stream << downloadReport;
    }
    else
    {
        stream << "--- Download ---\nNo browser attempts recorded.\n";
    }
    return stream.str();
}

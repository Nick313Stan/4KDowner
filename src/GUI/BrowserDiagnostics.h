#pragma once

#include <string>
#include <vector>

struct BrowserAttempt
{
    std::string browserSpec;
    bool success = false;
    std::string summary;
    std::string nextAction;
};

class BrowserAttemptLog
{
public:
    void Clear();
    void AddAttempt(BrowserAttempt attempt);
    void SetWinner(const std::string& browserSpec);

    const std::string& Winner() const;
    bool HasWinner() const;
    const std::vector<BrowserAttempt>& Attempts() const;
    bool Empty() const;

    std::string FormatSection(const std::string& title) const;

private:
    std::vector<BrowserAttempt> attempts_;
    std::string winner_;
    bool winnerSet_ = false;
};

std::string FormatBrowserAuthLabel(const std::string& browserSpec);
std::string SummarizeBrowserAttemptOutput(const std::string& output, bool parseMode);
std::string DescribeBrowserRetryAction(const std::string& output, bool hasMoreBrowsers, bool success);
std::string FormatBrowserSessionReport(const std::string& url,
                                       const std::string& title,
                                       const std::string& parseReport,
                                       const std::string& downloadReport);

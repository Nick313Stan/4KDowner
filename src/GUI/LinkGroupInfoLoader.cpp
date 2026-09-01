#include "LinkGroupInfoLoader.h"

#include <chrono>
#include <exception>
#include <future>
#include <mutex>
#include <utility>

namespace
{
std::mutex g_abandonedGroupFuturesMutex;
std::vector<std::future<LinkGroupInfo>> g_abandonedGroupFutures;

void AbandonGroupFuture(std::future<LinkGroupInfo>& future)
{
    if (!future.valid())
    {
        return;
    }

    if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            future.get();
        }
        catch (...)
        {
        }
        return;
    }

    std::lock_guard<std::mutex> lock(g_abandonedGroupFuturesMutex);
    g_abandonedGroupFutures.push_back(std::move(future));
}
} // namespace

LinkGroupInfoLoader::~LinkGroupInfoLoader()
{
    AbandonRunningWork();
}

LinkGroupInfoLoader::LinkGroupInfoLoader(LinkGroupInfoLoader&& other) noexcept
    : future_(std::move(other.future_)),
      cancelRequested_(std::move(other.cancelRequested_)),
      result_(std::move(other.result_)),
      isLoading_(other.isLoading_),
      hasResult_(other.hasResult_)
{
    other.isLoading_ = false;
    other.hasResult_ = false;
}

LinkGroupInfoLoader& LinkGroupInfoLoader::operator=(LinkGroupInfoLoader&& other) noexcept
{
    if (this != &other)
    {
        AbandonRunningWork();
        future_ = std::move(other.future_);
        cancelRequested_ = std::move(other.cancelRequested_);
        result_ = std::move(other.result_);
        isLoading_ = other.isLoading_;
        hasResult_ = other.hasResult_;
        other.isLoading_ = false;
        other.hasResult_ = false;
    }
    return *this;
}

void LinkGroupInfoLoader::AbandonRunningWork()
{
    if (cancelRequested_ != nullptr)
    {
        cancelRequested_->store(true);
    }
    AbandonGroupFuture(future_);
    cancelRequested_.reset();
    isLoading_ = false;
}

void LinkGroupInfoLoader::ReapAbandoned()
{
    std::lock_guard<std::mutex> lock(g_abandonedGroupFuturesMutex);
    auto it = g_abandonedGroupFutures.begin();
    while (it != g_abandonedGroupFutures.end())
    {
        if (!it->valid() || it->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                if (it->valid())
                {
                    it->get();
                }
            }
            catch (...)
            {
            }
            it = g_abandonedGroupFutures.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void LinkGroupInfoLoader::Start(std::string url)
{
    AbandonRunningWork();

    result_ = {};
    result_.url = url;
    hasResult_ = false;
    isLoading_ = true;
    cancelRequested_ = std::make_shared<std::atomic_bool>(false);

    future_ = std::async(std::launch::async,
                         [url = std::move(url), cancelRequested = cancelRequested_]
                         {
                             return Load(url, cancelRequested);
                         });
}

void LinkGroupInfoLoader::Cancel()
{
    AbandonRunningWork();
}

void LinkGroupInfoLoader::Update()
{
    if (!isLoading_ || !future_.valid())
    {
        return;
    }

    if (future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            result_ = future_.get();
        }
        catch (const std::exception& exception)
        {
            result_ = {};
            result_.error = exception.what();
        }
        catch (...)
        {
            result_ = {};
            result_.error = "Unexpected parser error.";
        }
        hasResult_ = true;
        isLoading_ = false;
        cancelRequested_.reset();
    }
}

bool LinkGroupInfoLoader::IsLoading() const
{
    return isLoading_;
}

bool LinkGroupInfoLoader::HasResult() const
{
    return hasResult_;
}

const LinkGroupInfo& LinkGroupInfoLoader::GetResult() const
{
    return result_;
}

/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CSaveWorkerPool.cpp
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CSaveWorkerPool.h"
#include <objbase.h>

CSaveWorkerPool::CSaveWorkerPool(int numThreads)
    : m_Stopping(false)
{
    if (numThreads < 1) numThreads = 1;
    m_Threads.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i)
        m_Threads.emplace_back(&CSaveWorkerPool::WorkerLoop, this);
}

CSaveWorkerPool::~CSaveWorkerPool()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Stopping = true;
    }
    m_Condvar.notify_all();
    for (std::thread& t : m_Threads)
    {
        if (t.joinable())
            t.join();
    }
}

std::future<void> CSaveWorkerPool::Submit(std::function<void()> task)
{
    std::packaged_task<void()> pkg(std::move(task));
    std::future<void>          fut = pkg.get_future();
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Tasks.push(std::move(pkg));
    }
    m_Condvar.notify_one();
    return fut;
}

void CSaveWorkerPool::WorkerLoop()
{
    // WIC requires COM on the calling thread. Workers use WIC via CModalityImageWriter.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    for (;;)
    {
        std::packaged_task<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Condvar.wait(lock, [this] { return m_Stopping || !m_Tasks.empty(); });

            if (m_Stopping && m_Tasks.empty())
                break;

            task = std::move(m_Tasks.front());
            m_Tasks.pop();
        }

        // packaged_task captures any exception into the returned future;
        // running it here cannot escape.
        task();
    }

    CoUninitialize();
}

/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CSaveWorkerPool.h
 *
 *  Minimal worker pool used by CMultiModalCapture to parallelize per-modality
 *  WIC encode + disk write while the main thread blocks inside
 *  captureMultiModalFrame().
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <condition_variable>
#include <future>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class CSaveWorkerPool
{
public:
    explicit CSaveWorkerPool(int numThreads);
    ~CSaveWorkerPool();

    CSaveWorkerPool(const CSaveWorkerPool&) = delete;
    CSaveWorkerPool& operator=(const CSaveWorkerPool&) = delete;

    // Queues `task` for execution on a worker thread. Returns a future that
    // becomes ready once the task has finished (or propagates its exception).
    std::future<void> Submit(std::function<void()> task);

private:
    void WorkerLoop();

    std::vector<std::thread>               m_Threads;
    std::queue<std::packaged_task<void()>> m_Tasks;
    std::mutex                             m_Mutex;
    std::condition_variable                m_Condvar;
    bool                                   m_Stopping;
};

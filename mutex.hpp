#ifndef __MUTEX_H__
#define __MUTEX_H__

#include <cassert>
#include <condition_variable>
#include <mutex>
#include "thread_annotations.h"

class CondVar;

// Thinly wraps std::mutex.
class LOCKABLE Mutex
{
public:
    Mutex() = default;
    ~Mutex() = default;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock() EXCLUSIVE_LOCK_FUNCTION() { mu_.lock(); }
    void Unlock() UNLOCK_FUNCTION() { mu_.unlock(); }
    void AssertHeld() ASSERT_EXCLUSIVE_LOCK() {}

private:
    friend class CondVar;
    std::mutex mu_;
};

class SCOPED_LOCKABLE MutexLock
{
public:
    explicit MutexLock(Mutex* mu) EXCLUSIVE_LOCK_FUNCTION(mu)
        : mu_(mu)
    {
        this->mu_->Lock();
    }
    ~MutexLock() UNLOCK_FUNCTION() { this->mu_->Unlock(); }

    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;

private:
    Mutex* const mu_;
};
// Thinly wraps std::condition_variable.
class CondVar
{
public:
    explicit CondVar(Mutex* mu)
        : mu_(mu)
    {
        assert(mu != nullptr);
    }
    ~CondVar() = default;

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;

    void Wait()
    {
        std::unique_lock<std::mutex> lock(mu_->mu_, std::adopt_lock);
        cv_.wait(lock);
        lock.release();
    }
    void Signal() { cv_.notify_one(); }
    void SignalAll() { cv_.notify_all(); }

private:
    std::condition_variable cv_;
    Mutex* const mu_;
};

#endif  //

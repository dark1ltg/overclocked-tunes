#pragma once

#include <atomic>

class JobGate {
public:
    bool tryLock()
    {
        bool expected = false;
        return m_busy.compare_exchange_strong(expected, true);
    }
    void unlock() { m_busy.store(false); }
    bool locked() const { return m_busy.load(); }

private:
    std::atomic<bool> m_busy{false};
};

class JobGateLock {
public:
    explicit JobGateLock(JobGate *gate)
        : m_gate(gate)
        , m_owned(!gate || gate->tryLock())
    {
    }
    ~JobGateLock()
    {
        if (m_gate && m_owned)
            m_gate->unlock();
    }
    JobGateLock(const JobGateLock &) = delete;
    JobGateLock &operator=(const JobGateLock &) = delete;
    explicit operator bool() const { return m_owned; }

private:
    JobGate *m_gate = nullptr;
    bool m_owned = false;
};

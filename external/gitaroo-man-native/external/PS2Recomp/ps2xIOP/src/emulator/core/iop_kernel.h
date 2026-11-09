#pragma once

#include "iop_cpu.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

namespace ps2x::iop::detail
{
    class IopMemory;

    enum class IopThreadState : uint8_t
    {
        Dormant,
        Ready,
        Running,
        Sleep,
        Delay,
        Semaphore,
        EventFlag,
        Suspended,
        Dead,
    };

    struct IopThread
    {
        int id = 0;
        IopThreadState state = IopThreadState::Dormant;
        IopCpuState cpu;
        uint32_t entry = 0;
        uint32_t stackBase = 0;
        uint32_t stackSize = 0;
        uint32_t priority = 0x40;
        uint32_t initialPriority = 0x40;
        uint32_t option = 0;
        uint32_t attr = 0;
        uint64_t wakeCycle = 0;
        // Place in its priority's ready queue (lower runs first): threads that become ready join the back, a
        // running thread that loses the CPU without blocking goes back to the front (threadman's
        // readyq_insert_back / readyq_insert_front).
        int64_t readyOrder = 0;
        int waitId = 0;
        uint32_t waitBits = 0;
        uint32_t waitMode = 0;
        uint32_t waitResultAddress = 0;
        int wakeupCount = 0;
    };

    class IopKernel
    {
    public:
        explicit IopKernel(IopMemory &memory) noexcept;

        void reset();

        [[nodiscard]] bool dispatchThreadImport(uint16_t ordinal, IopCpuState &cpu, uint64_t currentCycle);
        [[nodiscard]] bool dispatchSemaphoreImport(uint16_t ordinal, IopCpuState &cpu);
        [[nodiscard]] bool dispatchEventImport(uint16_t ordinal, IopCpuState &cpu);

        [[nodiscard]] int createInternalEventFlag(uint32_t attr, uint32_t option, uint32_t bits);
        [[nodiscard]] bool setInternalEventFlag(int id, uint32_t bits);

        void sleepCurrent(IopCpuState &cpu);
        void delayCurrentUntil(uint64_t wakeCycle, IopCpuState &cpu);

        [[nodiscard]] IopThread *beginNextReady(uint64_t currentCycle);
        // Makes thread `id` current again if it is still ready (it was only stopped by a timeslice end).
        [[nodiscard]] IopThread *resumeIfReady(int id);
        // The current thread's id (0 outside any thread), a thread's priority (-1 if it does not exist),
        // and the most urgent priority among ready threads (-1 if none is ready, delays due at `currentCycle`
        // counted as ready).
        [[nodiscard]] int currentThreadId() const noexcept { return m_currentThread ? m_currentThread->id : 0; }
        [[nodiscard]] int threadPriority(int id) const;
        [[nodiscard]] int bestReadyPriority(uint64_t currentCycle) const;
        // Code running in a nested synchronous call (no current thread) that would block instead asks
        // the emulator to run the other IOP threads until `ready(currentCycle)` holds (bounded).
        using NestedWaitFn = std::function<void(const std::function<bool(uint64_t)> &ready)>;
        void setNestedWaitHandler(NestedWaitFn handler) { m_nestedWait = std::move(handler); }

        // Nested synchronous guest calls (RPC servers, callbacks) run outside any IOP thread.
        IopThread *exchangeCurrentThread(IopThread *thread) noexcept
        {
            IopThread *previous = m_currentThread;
            m_currentThread = thread;
            return previous;
        }
        [[nodiscard]] uint64_t nextWakeCycle(uint64_t fallback) const;
        void endTimeslice(IopThread &thread, uint32_t returnSentinel);
        void cleanupDeadThreads();
        void terminateThreadsInRange(uint32_t base, uint32_t size);

        [[nodiscard]] size_t threadCount() const noexcept { return m_threads.size(); }

    private:
        struct Semaphore
        {
            int id = 0;
            uint32_t attr = 0;
            uint32_t option = 0;
            int current = 0;
            int maximum = 1;
        };

        struct EventFlag
        {
            int id = 0;
            uint32_t bits = 0;
            uint32_t attr = 0;
            uint32_t option = 0;
        };

        [[nodiscard]] bool referThreadStatus(int id, uint32_t outputAddress);
        void wakeOneSemaphore(int id);
        [[nodiscard]] static bool eventSatisfied(const EventFlag &event, uint32_t bits, uint32_t mode);
        void wakeEventWaiters(EventFlag &event);
        void makeReady(IopThread &thread) noexcept
        {
            thread.state = IopThreadState::Ready;
            thread.readyOrder = ++m_readyBack;
        }

        IopMemory &m_memory;
        std::map<int, IopThread> m_threads;
        std::map<int, Semaphore> m_semaphores;
        std::map<int, EventFlag> m_eventFlags;
        uint32_t m_nextThreadId = 1;
        uint32_t m_nextSemaphoreId = 1;
        uint32_t m_nextEventFlagId = 1;
        IopThread *m_currentThread = nullptr;
        NestedWaitFn m_nestedWait;
        int64_t m_readyBack = 0;
        int64_t m_readyFront = 0;
    };
}

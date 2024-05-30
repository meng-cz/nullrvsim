
#include <assert.h>

#include "spinlocks.h"

using std::atomic;
using std::atomic_int;
using std::atomic_store_explicit;
using std::atomic_load_explicit;
using std::atomic_compare_exchange_weak_explicit;
using std::memory_order_relaxed;
using std::memory_order_acquire;
using std::memory_order_release;

void SpinRWLock::read_lock()
{
    int expected;
    int desired;

    while(true)
    {
        expected = atomic_load_explicit(&lock, memory_order_relaxed);

        if(expected >= 0)
        {
            desired = 1 + expected;
            if(atomic_compare_exchange_weak_explicit(&lock, &expected, desired, memory_order_relaxed, memory_order_relaxed))
                break; // success
        }

        for(uint64_t i = 0; i < wait_interval; i++) {
            __asm__ volatile("nop");
        }
    }

    atomic_thread_fence(memory_order_acquire); // sync
}

void SpinRWLock::read_unlock()
{
    int expected;
    int desired;

    while(true)
    {
        expected = atomic_load_explicit(&lock, memory_order_relaxed);

        if(expected > 0)
        {
            desired = expected - 1;

            atomic_thread_fence(memory_order_release); // sync
            if(atomic_compare_exchange_weak_explicit(&lock, &expected, desired, memory_order_relaxed, memory_order_relaxed))
                break; // success
        }
        else
        {
            assert(false);
        }
    }
}

void SpinRWLock::write_lock()
{
    int expected;
    int desired;

    while(true)
    {
        expected = atomic_load_explicit(&lock, memory_order_relaxed);

        if(expected == 0)
        {
            desired = -1;
            if(atomic_compare_exchange_weak_explicit(&lock, &expected, desired, memory_order_relaxed, memory_order_relaxed))
                break; // success
        }

        for(uint64_t i = 0; i < wait_interval; i++) {
            __asm__ volatile("nop");
        }
    }

    atomic_thread_fence(memory_order_release); // sync
}

void SpinRWLock::write_unlock()
{
    int expected;
    int desired;

    while(true)
    {
        expected = atomic_load_explicit(&lock, memory_order_relaxed);

        if(expected == -1)
        {
            desired = 0;

            atomic_thread_fence(memory_order_release); // sync
            if(atomic_compare_exchange_weak_explicit(&lock, &expected, desired, memory_order_relaxed, memory_order_relaxed))
                break; // success
        }
        else
        {
            assert(false);
        }
    }
}


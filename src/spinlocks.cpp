// MIT License

// Copyright (c) 2024 Meng Chengzhen, in Shandong University

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


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


#ifndef RVSIM_LOCKS_H
#define RVSIM_LOCKS_H

#include <atomic>

#include <pthread.h>
#include <assert.h>

#define HOST_CACHE_LEN_BYTE (64)

class DefaultLock {
public:
    DefaultLock() {pthread_mutex_init(&mutex, nullptr);}
    ~DefaultLock() {pthread_mutex_destroy(&mutex);}
    inline void lock() {
        pthread_mutex_lock(&mutex);
    }
    inline void unlock() {
        pthread_mutex_unlock(&mutex);
    }
private:
    pthread_mutex_t mutex;
};

class alignas(HOST_CACHE_LEN_BYTE) SpinLock {
public:
    SpinLock() {};
    SpinLock(uint64_t interval) : wait_interval(interval) {};
    void lock() {
        while (locked.test_and_set(std::memory_order_acquire)) { 
            for(uint64_t i = 0; i < wait_interval; i++) {
                __asm__ volatile("nop");
            }
        }
    }
    void unlock() {
        locked.clear(std::memory_order_release);
    }
    uint64_t wait_interval = 1024UL;
protected:
    std::atomic_flag locked = ATOMIC_FLAG_INIT ;
    uint8_t pad[HOST_CACHE_LEN_BYTE - sizeof(locked)];
};

class alignas(HOST_CACHE_LEN_BYTE) SpinRWLock {
public:
    SpinRWLock () {
        std::atomic_store_explicit(&lock, 0, std::memory_order_relaxed);
    }
    void read_lock();
    void read_unlock();
    void write_lock();
    void write_unlock();
    uint64_t wait_interval = 1024UL;
protected:
    std::atomic<int> lock;
    uint8_t pad[HOST_CACHE_LEN_BYTE - sizeof(lock)];
};

class DefaultBarrier {
public:
    DefaultBarrier(unsigned int n) {
        pthread_barrier_init(&b, nullptr, n);
    }
    ~DefaultBarrier() {
        pthread_barrier_destroy(&b);
    }

    void wait() {
        pthread_barrier_wait(&b);
    }
protected:
    pthread_barrier_t b;
};

class alignas(HOST_CACHE_LEN_BYTE) SpinBarrier
{
public:
    SpinBarrier (unsigned int n) : n_ (n), nwait_ (0), step_(0) {}

    void wait ()
    {
        unsigned int step = step_.load ();

        if (nwait_.fetch_add (1) == n_ - 1)
        {
            nwait_.store (0); // XXX: maybe can use relaxed ordering here ??
            step_.fetch_add (1);
            return ;
        }
        else
        {
            while (step_.load () == step) {
                for(uint64_t i = 0; i < wait_interval; i++) {
                    __asm__ volatile("nop");
                }
            }
            return ;
        }
    }

    uint64_t wait_interval = 1024UL;
protected:

    /* Number of synchronized threads. */
    const unsigned int n_;

    /* Number of threads currently spinning.  */
    std::atomic<unsigned int> nwait_;

    uint8_t pad0[HOST_CACHE_LEN_BYTE - sizeof(nwait_) - sizeof(n_)];

    /* Number of barrier syncronizations completed so far, 
     * it's OK to wrap.  */
    std::atomic<unsigned int> step_;
    
    uint8_t pad1[HOST_CACHE_LEN_BYTE - sizeof(step_)];
};

#endif

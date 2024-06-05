#ifndef RVSIM_TICKQUEUE_H
#define RVSIM_TICKQUEUE_H

#include "common.h"
#include "spinlocks.h"

template <typename T>
class ValidData {
public:
    bool valid = false;
    T data;
};

/**
 * 延迟1周期写入的列表结构
 * 每周期最多向列表中写入input_width项，写入的项会在下一周期进入主列表
*/
template <typename T>
class LimitedTickList {
public:
    LimitedTickList(uint32_t input_wid, uint32_t queue_size) {
        assert(input_wid > 0);
        assert(input_wid < (1<<30));
        iwid = input_wid;
        qsize = queue_size;
    }
    inline uint32_t can_push() {
        return ((push_buf.size() < iwid && push_buf.size() + q.size() < qsize)?(iwid - push_buf.size()):0);
    }
    inline bool push_next_tick(T &v) {
        if(can_push() == 0) return false;
        push_buf.push_back(v);
        return true;
    }
    inline void apply_next_tick() {
        q.splice(q.end(), push_buf);
    }
    /**
     * 获取当前的主列表内容
    */
    inline std::list<T> &get() {return q;};
    /**
     * 元素总个数
    */
    inline uint64_t size() {return q.size() + push_buf.size();}
    /**
     * 主列表中的元素个数
    */
    inline uint64_t cur_size() {return q.size();}
    inline bool empty() {return (q.empty() && push_buf.empty());}
    inline T &front() {return q.front();}
    inline void pop_front() {q.pop_front();}
    inline void clear() {q.clear(); push_buf.clear();}
    inline void clear(std::list<T> *to_free) {
        if(to_free) {
            to_free->splice(to_free->end(), q);
            to_free->splice(to_free->end(), push_buf);
        }
        else {
            clear();
        }
    }
protected:
    uint32_t iwid, qsize;
    std::list<T> push_buf;
    std::list<T> q;
};

/**
 * 延迟1周期写入的列表结构，写入的项会在下一周期进入主列表
*/
template <typename T>
class TickList {
public:
    inline bool push_next_tick(T &v) {
        push_buf.push_back(v);
        return true;
    }
    inline void apply_next_tick() {
        q.splice(q.end(), push_buf);
    }
    /**
     * 获取当前的主列表内容
    */
    inline std::list<T> &get() {return q;};
    /**
     * 元素总个数
    */
    inline uint64_t size() {return q.size() + push_buf.size();}
    /**
     * 主列表中的元素个数
    */
    inline uint64_t cur_size() {return q.size();}
    inline bool empty() {return (q.empty() && push_buf.empty());}
    inline T &front() {return q.front();}
    inline void pop_front() {q.pop_front();}
    inline void clear() {q.clear(); push_buf.clear();}
    inline void clear(std::list<T> *to_free) {
        if(to_free) {
            to_free->splice(to_free->end(), q);
            to_free->splice(to_free->end(), push_buf);
        }
        else {
            clear();
        }
    }
protected:
    std::list<T> push_buf;
    std::list<T> q;
};

template <typename K, typename T>
class TickMap {
public:
    inline void apply_next_tick() {
        for(auto &entry : push_buf) {
            m[entry.first] = entry.second;
        }
        push_buf.clear();
    }
    inline void push_next_tick(K &k, T &v) {
        push_buf.emplace_back(k, v);
    }
    inline std::unordered_map<K,T> &get() {return m;}
    inline bool empty() {return (m.empty() && push_buf.empty());}
    inline uint64_t size() {return m.size() + push_buf.size();}
    inline uint64_t cur_size() {return m.size();}
    inline void clear() {m.clear(); push_buf.clear();}
    inline void clear(std::list<std::pair<K,T>> *to_free) {
        if(to_free) {
            for(auto &entry : m) {
                to_free->emplace_back(entry.first, entry.second);
            }
            to_free->splice(to_free->end(), push_buf);
        }
        clear();
    }
protected:
    std::list<std::pair<K,T>> push_buf;
    std::unordered_map<K,T> m;
};

template <typename K, typename T>
class TickMultiMap {
public:
    inline void apply_next_tick() {
        for(auto &entry : push_buf) {
            m.emplace(entry);
        }
        push_buf.clear();
    }
    inline void push_next_tick(K &k, T &v) {
        push_buf.emplace_back(k, v);
    }
    inline std::unordered_multimap<K,T> &get() {return m;}
    inline bool empty() {return (m.empty() && push_buf.empty());}
    inline uint64_t size() {return m.size() + push_buf.size();}
    inline uint64_t cur_size() {return m.size();}
    inline void clear() {m.clear(); push_buf.clear();}
    inline void clear(std::list<std::pair<K,T>> *to_free) {
        if(to_free) {
            for(auto &entry : m) {
                to_free->emplace_back(entry.first, entry.second);
            }
            to_free->splice(to_free->end(), push_buf);
        }
        clear();
    }
protected:
    std::list<std::pair<K,T>> push_buf;
    std::unordered_multimap<K,T> m;
};


template <typename T>
class TickSet {
public:
    inline void apply_next_tick() {
        for(auto &entry : push_buf) {
            s.emplace(entry);
        }
        push_buf.clear();
    }
    inline void push_next_tick(T &v) {
        push_buf.emplace(v);
    }
    inline std::set<T> &get() {return s;}
    inline bool empty() {return (s.empty() && push_buf.empty());}
    inline uint64_t size() {return s.size() + push_buf.size();}
    inline uint64_t cur_size() {return s.size();}
    inline void clear() {s.clear(); push_buf.clear();}
    inline void clear(std::list<T> *to_free) {
        if(to_free) {
            for(auto &entry : s) {
                to_free->emplace_back(entry);
            }
            to_free->splice(to_free->end(), push_buf);
        }
        clear();
    }
protected:
    std::set<T> push_buf;
    std::set<T> s;
};


template <typename T>
class SimpleTickQueue : public SimObject {

public:

    SimpleTickQueue(uint32_t input_wid, uint32_t output_wid, uint32_t queue_size) {
        assert(input_wid > 0);
        assert(output_wid > 0);
        assert(input_wid < (1<<30));
        assert(output_wid < (1<<30));
        iwid = input_wid;
        owid = output_wid;
        qsize = queue_size;
        do_on_current_tick = 0;
        do_apply_next_tick = 1;
    }
    ~SimpleTickQueue() {}
    virtual void apply_next_tick() {
        while(pop_buf.size() < owid && !q.empty()) pop_buf.splice(pop_buf.end(), q, q.begin());
        while(pop_buf.size() < owid && !push_buf.empty()) pop_buf.splice(pop_buf.end(), push_buf, push_buf.begin());
        while(q.size() < qsize && !push_buf.empty()) q.splice(q.end(), push_buf, push_buf.begin());
    }

    inline uint32_t input_wid() {return iwid;}
    inline uint32_t output_wid() {return owid;}
    inline uint32_t can_push() {return ((push_buf.size() < iwid)?(iwid - push_buf.size()):0);}
    inline uint32_t can_pop() {return std::min<uint32_t>(owid, pop_buf.size());}

    inline bool push(T &v) {
        if(push_buf.size() >= iwid) return false;
        push_buf.emplace_back(v);
        return true;
    }
    inline T& top() {
        return pop_buf.front();
    }
    inline void pop() {
        pop_buf.pop_front();
    }

    inline void clear(std::list<T> *out = nullptr) {
        if(out) {
            out->splice(out->end(), pop_buf);
            out->splice(out->end(), q);
            out->splice(out->end(), push_buf);
        }
        push_buf.clear();
        q.clear();
        pop_buf.clear();
    }

    inline bool empty() {
        return (push_buf.empty() && pop_buf.empty() && q.empty());
    }

    inline bool pass_to(SimpleTickQueue<T> &q2, uint32_t cnt = 1) {
        if(cnt == 0) return true;
        if(can_pop() < cnt || q2.can_push() < cnt) return false;
        auto it = pop_buf.begin();
        std::advance(it, cnt);
        q2.push_buf.splice(q2.push_buf.end(), pop_buf, pop_buf.begin(), it);
        return true;
    }

    inline uint64_t size() {
        return (push_buf.size() + q.size() + pop_buf.size());
    }
    inline uint64_t cur_size() {
        return (q.size() + pop_buf.size());
    }

    inline void dbg_get_all(std::vector<T> *out) {
        out->reserve(push_buf.size() + q.size() + pop_buf.size());
        for(auto &e : push_buf) out->emplace_back(e);
        for(auto &e : q) out->emplace_back(e);
        for(auto &e : pop_buf) out->emplace_back(e);
    }

protected:
    uint32_t iwid, owid, qsize;
    std::list<T> push_buf;
    std::list<T> pop_buf;
    std::list<T> q;
};

// template <typename T, int IWid = 1, int OWid = 1, int QSize = 1>
// class OneTickQueue : public SimObject {
// public:
//     OneTickQueue() {
//         static_assert(IWid > 0);
//         static_assert(OWid > 0);
//         static_assert(QSize > 0);
//         static_assert(QSize >= IWid);
//         static_assert(QSize >= OWid);
//         do_on_current_tick = 0;
//         do_apply_next_tick = 1;
//     }

//     virtual void apply_next_tick() {
//         lock.lock();
//         to_push_cnt = std::min<uint32_t>(to_push_cnt, IWid);
//         to_pop_cnt = std::min<uint32_t>(to_pop_cnt, OWid);
//         to_pop_cnt = std::min<uint32_t>(to_pop_cnt, q.size());
//         uint32_t wb_cnt = std::min<uint32_t>(OWid, q.size());
//         for(uint32_t i = 0; i < to_pop_cnt; i++) {
//             q.pop_front();
//         }
//         {
//             auto iter = q.begin();
//             for(uint32_t i = to_pop_cnt; i < wb_cnt; i++, iter++) {
//                 *iter = top_buf[i];
//             }
//         }
//         for(uint32_t i = 0; i < to_push_cnt; i++) {
//             q.push_back(push_buf[i]);
//         }
//         uint32_t can_pop = std::min<uint32_t>(OWid, q.size());
//         auto iter = q.begin();
//         for(uint32_t i = 0; i < can_pop; i++, iter++) {
//             top_buf[i] = (*iter);
//         }
//         to_push_cnt = to_pop_cnt = 0;
//         if(q.size() > QSize) {
//             to_push_cnt = q.size() - QSize;
//             for(uint32_t i = to_push_cnt - 1; 1; i--) {
//                 push_buf[i] = q.back();
//                 q.pop_back();
//                 if(i == 0) break;
//             }
//         }
//         lock.unlock();
//     };
    
//     inline uint32_t input_width() {
//         return IWid;
//     }
//     inline uint32_t can_push_cnt() {
//         lock.lock();
//         uint32_t ret = ((to_push_cnt < IWid)?(IWid - to_push_cnt):0);
//         lock.unlock();
//         return ret;
//     }
//     inline void push(T &v) {
//         lock.lock();
//         if(to_push_cnt < IWid) {
//             push_buf[to_push_cnt] = v;
//             to_push_cnt++;
//         }
//         lock.unlock();
//     }
//     uint32_t to_push_cnt = 0;
//     T push_buf[IWid];

//     inline uint32_t output_width() {
//         return OWid;
//     }
//     inline uint32_t can_pop_cnt() {
//         lock.lock();
//         uint32_t ret = std::min<uint32_t>(OWid, q.size());
//         lock.unlock();
//         return ret;
//     };
//     inline void pop(uint32_t cnt = 1) {
//         lock.lock();
//         to_pop_cnt = std::min<uint32_t>(OWid, to_pop_cnt + cnt);
//         lock.unlock();
//     };
//     T top_buf[OWid];
//     uint32_t to_pop_cnt = 0;

//     void clear() {
//         lock.lock();
//         q.clear();
//         to_push_cnt = to_pop_cnt = 0;
//         lock.unlock();
//     };

//     void debug_get_queue(std::list<T> *out) {
//         out->clear();
//         out->assign(q.begin(), q.end());
//     }

// protected:
//     SpinLock lock;
//     std::list<T> q;
// };

namespace test {

bool test_tickqueue();

}

#endif

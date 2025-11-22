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

#include "simroot.h"
#include "configuration.h"
#include "spinlocks.h"

#include <filesystem>

namespace simroot {

typedef struct {
    std::ofstream *ofile = nullptr;
    std::list<string> buf;
    uint64_t lineoffset = 0;
    int32_t linecnt = 0;
} LogFile;

inline void log_file_close(LogFile *p) {
    if(p->linecnt > 0) {
        for(auto &str : p->buf) {
            *(p->ofile) << str << "\n";
        }
    }
    p->ofile->close();
    delete p;
}

typedef struct alignas(64) {
    std::string name;
    SimObject *p_obj = nullptr;
    int latency = 0;
    int current = 0;

    uint8_t pad[64 - sizeof(name) - sizeof(p_obj) - sizeof(latency) - sizeof(current)];
} SimObjectWithFreq;

typedef struct alignas(64) {
    std::vector<SimObjectWithFreq> simobjs;
    double cur_sum = 0;
    double apl_sum = 0;

    bool do_clear_statistic = false;

    uint8_t pad[64 - sizeof(simobjs) - sizeof(cur_sum) - sizeof(apl_sum) - sizeof(do_clear_statistic)];
} SimRootThreadTask;

struct SimRootThreadTaskCmpCur {
    bool operator()(SimRootThreadTask* a, SimRootThreadTask* b) {
        return a->cur_sum > b->cur_sum;
    }
};
struct SimRootThreadTaskCmpApl {
    bool operator()(SimRootThreadTask* a, SimRootThreadTask* b) {
        return a->apl_sum > b->apl_sum;
    }
};

class SimRoot {
public:
    SimRoot(uint32_t thread_num) : barrier(thread_num), thread_num(thread_num) {
        tasks = new SimRootThreadTask[thread_num];
        ths = new pthread_t[thread_num];
        // barrier.wait_interval = conf::get_int("root", "barrier_wait_interval", 1024);
        lock_log.wait_interval = conf::get_int("root", "log_lock_wait_interval", 1024);

        global_freq = conf::get_int("root", "global_freq_mhz", 1000) * 1000000UL;
        wall_time_freq = conf::get_int("root", "wall_time_freq_mhz", 1000) * 1000000UL;
        start_time_us = get_current_time_us();

        std::string log_dir = conf::get_str("root", "out_dir", "out");
        std::filesystem::create_directories(log_dir);
        stdout_logfile = std::ofstream(log_dir + "/stdout.txt", std::ios::out);
        stderr_logfile = std::ofstream(log_dir + "/stderr.txt", std::ios::out);
            
    };
    ~SimRoot() {
        stdout_logfile.close();
        stderr_logfile.close();
        delete[] tasks;
        delete[] ths;
    }

    uint32_t thread_num;

    std::vector<SimObjectWithFreq> all_sim_objs;

    SimRootThreadTask *tasks = nullptr;
    uint32_t insert_thread_idx = 0;

    SpinLock lock_log;

    SpinBarrier barrier;
    pthread_t *ths = nullptr;

    bool is_processing = false;
    uint64_t current_tick = 0;
    uint64_t global_freq = 0;
    uint64_t start_time_us = 0;
    uint64_t wall_time_freq = 0;

    uint64_t last_1mtick_real_time_us = 0;
    uint64_t last_1mtick_real_time_interval_us = 0;

    std::set<LogFile*> logfiles;

    std::ofstream stdout_logfile;
    std::ofstream stderr_logfile;
};

SimRoot *root = nullptr;

void int_signal_handler(int signum) {
    printf("Recieve SIGINT\n");
    dump_core();
    exit(-1);
}

void init_simroot() {
    if(root == nullptr) {
        root = new SimRoot(conf::get_int("root", "thread_num", 4));
        signal(SIGINT, int_signal_handler);
    }
}

void add_sim_object(SimObject *p_obj, std::string name, int latency) {
    init_simroot();
    SimObjectWithFreq tmp{
        .name = name,
        .p_obj = p_obj,
        .latency = latency,
        .current = ((latency > 0)?(latency - 1):0)
    };
    root->all_sim_objs.push_back(tmp);
    if(latency) {
        double cur = p_obj->do_on_current_tick, apl = p_obj->do_apply_next_tick;
        SimRootThreadTask * t = root->tasks + root->insert_thread_idx;
        t->simobjs.push_back(tmp);
        t->cur_sum += cur;
        t->apl_sum += apl;
    }
}

void add_sim_object_next_thread(SimObject *p_obj, std::string name, int latency) {
    add_sim_object(p_obj, name, latency);
    root->insert_thread_idx = (root->insert_thread_idx + 1) % root->thread_num;
}

void clear_sim_object() {
    init_simroot();
    for(int i = 0; i < root->thread_num; i++) {
        root->tasks[i].simobjs.clear();
        root->tasks[i].cur_sum = root->tasks[i].apl_sum = 0;
    }
    root->all_sim_objs.clear();
}

void __attribute__((noinline)) sync_cur() {
    root->barrier.wait();
}

void __attribute__((noinline)) sync_apl() {
    root->barrier.wait();
}


void* simroot_thread_function(void *param) {
    uint64_t index = (uint64_t)param;

    std::string s = "Thread " + std::to_string(index) + ": ";
    for(auto &entry : root->tasks[index].simobjs) {
        s = s + entry.name + ", ";
    }
    print_log_info(s);

    while(root->is_processing) {
        
        for(auto &entry : root->tasks[index].simobjs) {
            if(entry.current == 0 && entry.p_obj->do_on_current_tick) {
                entry.p_obj->on_current_tick();
            }
        }

        sync_cur();

        for(auto &entry : root->tasks[index].simobjs) {
            if(entry.current == 0) {
                if(entry.p_obj->do_apply_next_tick) entry.p_obj->apply_next_tick();
                entry.current = entry.latency;
            }
            entry.current--;
        }
        
        if(index == 0) {
            root->current_tick++;
            if(root->current_tick % 1000000 == 0) [[unlikely]] {
                uint64_t rt = get_current_time_us();
                if(root->last_1mtick_real_time_us == 0) {
                    root->last_1mtick_real_time_us = rt;
                }
                else {
                    uint64_t interval = rt - root->last_1mtick_real_time_us;
                    root->last_1mtick_real_time_us = rt;
                    if(root->last_1mtick_real_time_interval_us == 0) {
                        root->last_1mtick_real_time_interval_us = interval;
                    }
                    else {
                        root->last_1mtick_real_time_interval_us = interval / 2 + root->last_1mtick_real_time_interval_us / 2;
                    }
                }
            }
        }
        if(root->tasks[index].do_clear_statistic) {
            for(auto &entry : root->tasks[index].simobjs) {
                entry.p_obj->clear_statistic();
            }
            root->tasks[index].do_clear_statistic = false;
        }

        sync_apl();

    }

    std::string s2 = "Simulator Thread " + std::to_string(index) + " Exited";
    print_log_info(s2);

    return nullptr;
}

void start_sim() {
    
    init_simroot();
    std::string log_dir = conf::get_str("root", "out_dir", "out");
    std::filesystem::create_directories(log_dir);

    {
        std::ofstream setup_log_file(log_dir + "/setup.txt", std::ios::out);
        for(auto &entry: root->all_sim_objs) {
            setup_log_file << entry.name << std::string(" @") << std::to_string(entry.latency) << std::string("tick\n");
            entry.p_obj->print_setup_info(setup_log_file);
            setup_log_file << std::endl;
        }
        setup_log_file.close();
    }

    root->is_processing = true;
    for(int i = 0; i < root->thread_num; i++) {
        root->tasks[i].do_clear_statistic = false;
        pthread_create(root->ths + i, nullptr, simroot_thread_function, (void*)((uint64_t)i));
    }

    for(int i = 0; i < root->thread_num; i++) {
        pthread_join(root->ths[i], nullptr);
    }

    {
        std::ofstream statistic_log_file(log_dir + "/statistic.txt", std::ios::out);
        for(auto &entry: root->all_sim_objs) {
            statistic_log_file << entry.name << std::string(" Latency:") << std::to_string(entry.latency) << std::string("\n");
            entry.p_obj->print_statistic(statistic_log_file);
            statistic_log_file << std::endl;
        }
        statistic_log_file.close();
    }

    std::cout << "Stop at " << root->current_tick << " Ticks" << std::endl;

    clear_sim_object();
}

void stop_sim_and_exit() {
    root->is_processing = false;
}

void clear_statistic() {
    if(root->is_processing) {
        for(int i = 0; i < root->thread_num; i++) {
            root->tasks[i].do_clear_statistic = true;
        }
    }
}

void print_log_info(std::string str) {
    init_simroot();
    root->lock_log.lock();
    LOG(INFO) << root->current_tick << ": " << str;
    root->lock_log.unlock();
}

void dump_core() {
    init_simroot();
    string path = conf::get_str("root", "core_path", "core.txt");
    std::ofstream ofile(path);

    for(auto &entry: root->all_sim_objs) {
        ofile << entry.name << ":\n";
        entry.p_obj->dump_core(ofile);
        ofile << std::endl;
    }

    for(auto &f : root->logfiles) {
        log_file_close(f);
    }
    root->logfiles.clear();

    printf("Core file at %s\n", path.c_str());
}

LogFileT create_log_file(string path, int32_t linecnt) {
    init_simroot();
    LogFile *ret = new LogFile;
    ret->ofile = new std::ofstream(path);
    ret->linecnt = linecnt;
    root->logfiles.insert(ret);
    return (LogFileT)ret;
}

void log_line(LogFileT fd, string &str) {
    LogFile *p = (LogFile *)fd;
    if(p->linecnt > 0) {
        p->buf.push_back(str);
        if(p->buf.size() > p->linecnt) {
            p->buf.pop_front();
            p->lineoffset++;
        }
    }
    else if(p->linecnt < 0) {
        *(p->ofile) << str << std::endl;
    }
}
void log_line(LogFileT fd, const char* cstr) {
    LogFile *p = (LogFile *)fd;
    if(p->linecnt > 0) {
        p->buf.push_back(cstr);
        if(p->buf.size() > p->linecnt) {
            p->buf.pop_front();
            p->lineoffset++;
        }
    }
    else if(p->linecnt < 0) {
        *(p->ofile) << cstr << std::endl;
    }
}

void destroy_log_file(LogFileT fd) {
    LogFile *p = (LogFile *)fd;
    root->logfiles.erase(p);
    log_file_close(p);
}

void log_stdout(const char *buf, uint64_t sz) {
    root->stdout_logfile.write(buf, sz);
    root->stdout_logfile.flush();
}

void log_stderr(const char *buf, uint64_t sz) {
    root->stderr_logfile.write(buf, sz);
    root->stderr_logfile.flush();
}


void set_current_tick(uint64_t tick) {
    init_simroot();
    root->current_tick = tick;
}

uint64_t get_current_tick() {
    init_simroot();
    return root->current_tick;
}

uint64_t get_wall_time_tick() {
    uint64_t tick = get_current_tick();
    double tick_to_walltime = (double)(root->wall_time_freq) / (double)(root->global_freq);
    return (uint64_t)((double)tick * tick_to_walltime);
};

uint64_t get_global_freq() {
    init_simroot();
    return root->global_freq;
}

uint64_t get_wall_time_freq() {
    init_simroot();
    return root->wall_time_freq;
}

uint64_t get_sim_time_us() {
    uint64_t tick = get_current_tick();
    return root->start_time_us + (tick / (root->global_freq / 1000000UL));
}

uint64_t get_sim_tick_per_real_sec() {
    if(root->last_1mtick_real_time_interval_us) {
        double tick_interval_us = (root->last_1mtick_real_time_interval_us) / 1000000.;
        return (1000000. / tick_interval_us);
    }
    else {
        return get_global_freq();
    }
}


}


namespace test{


bool test_simroot() {
    
    return false;
}

}

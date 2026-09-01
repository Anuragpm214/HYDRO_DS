#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

struct PerfCounters {
    uint64_t cycles = 0;
    uint64_t instructions = 0;
    uint64_t l1d_read_access = 0;
    uint64_t l1d_read_miss = 0;
    uint64_t llc_read_access = 0;
    uint64_t llc_read_miss = 0;
    uint64_t branch_instructions = 0;
    uint64_t branch_misses = 0;

    double ipc() const {
        return cycles > 0 ? static_cast<double>(instructions) / static_cast<double>(cycles) : 0.0;
    }
    double l1_miss_rate() const {
        return l1d_read_access > 0 ? (static_cast<double>(l1d_read_miss) / static_cast<double>(l1d_read_access)) * 100.0 : 0.0;
    }
    double llc_miss_rate() const {
        return llc_read_access > 0 ? (static_cast<double>(llc_read_miss) / static_cast<double>(llc_read_access)) * 100.0 : 0.0;
    }
    double branch_miss_rate() const {
        return branch_instructions > 0 ? (static_cast<double>(branch_misses) / static_cast<double>(branch_instructions)) * 100.0 : 0.0;
    }
};

class PerfProfiler {
private:
    struct EventDescriptor {
        std::string name;
        uint32_t type;
        uint64_t config;
        int fd = -1;
    };

    std::vector<EventDescriptor> events_;
    bool enabled_ = false;

    static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                               int cpu, int group_fd, unsigned long flags) {
        return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    }

    int open_event(uint32_t type, uint64_t config, int group_fd = -1) {
        struct perf_event_attr pe;
        std::memset(&pe, 0, sizeof(struct perf_event_attr));
        pe.type = type;
        pe.size = sizeof(struct perf_event_attr);
        pe.config = config;
        pe.disabled = 1;
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;

        int fd = perf_event_open(&pe, 0, -1, group_fd, 0);
        return fd;
    }

public:
    PerfProfiler() {
        // Define hardware and cache events
        events_ = {
            {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, -1},
            {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, -1},
            {"l1d_read_access", PERF_TYPE_HW_CACHE, 
                (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16)), -1},
            {"l1d_read_miss", PERF_TYPE_HW_CACHE, 
                (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)), -1},
            {"llc_read_access", PERF_TYPE_HW_CACHE, 
                (PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16)), -1},
            {"llc_read_miss", PERF_TYPE_HW_CACHE, 
                (PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)), -1},
            {"branch_instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS, -1},
            {"branch_misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES, -1}
        };

        for (auto& ev : events_) {
            ev.fd = open_event(ev.type, ev.config);
        }
    }

    ~PerfProfiler() {
        for (auto& ev : events_) {
            if (ev.fd >= 0) {
                close(ev.fd);
                ev.fd = -1;
            }
        }
    }

    bool is_supported() const {
        for (const auto& ev : events_) {
            if (ev.fd < 0) return false;
        }
        return true;
    }

    void start() {
        for (auto& ev : events_) {
            if (ev.fd >= 0) {
                ioctl(ev.fd, PERF_EVENT_IOC_RESET, 0);
                ioctl(ev.fd, PERF_EVENT_IOC_ENABLE, 0);
            }
        }
        enabled_ = true;
    }

    PerfCounters stop() {
        PerfCounters result;
        if (!enabled_) return result;

        for (auto& ev : events_) {
            if (ev.fd >= 0) {
                ioctl(ev.fd, PERF_EVENT_IOC_DISABLE, 0);
                uint64_t val = 0;
                if (read(ev.fd, &val, sizeof(uint64_t)) == sizeof(uint64_t)) {
                    if (ev.name == "cycles") result.cycles = val;
                    else if (ev.name == "instructions") result.instructions = val;
                    else if (ev.name == "l1d_read_access") result.l1d_read_access = val;
                    else if (ev.name == "l1d_read_miss") result.l1d_read_miss = val;
                    else if (ev.name == "llc_read_access") result.llc_read_access = val;
                    else if (ev.name == "llc_read_miss") result.llc_read_miss = val;
                    else if (ev.name == "branch_instructions") result.branch_instructions = val;
                    else if (ev.name == "branch_misses") result.branch_misses = val;
                }
            }
        }
        enabled_ = false;
        return result;
    }
};

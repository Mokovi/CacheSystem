// CacheBenchmark.hpp
#pragma once

#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <cmath>
#include <type_traits>
#include "CachePolicy.hpp"

template<typename Key, typename Value>
struct CacheStats {
    size_t total_ops        = 0;
    size_t total_get        = 0;
    size_t total_put        = 0;
    size_t cache_hits       = 0;
    size_t cache_misses     = 0;
    double hit_rate         = 0.0;   // %
    double miss_rate        = 0.0;   // %
    double ops_per_sec      = 0.0;   // ops/s
    double avg_access_time  = 0.0;   // ns
    double stddev_ns        = 0.0;   // ns
    double elapsed_ms       = 0.0;   // ms
};

template<typename Key, typename Value>
class CacheBenchmark {
    static_assert(std::is_integral<Key>::value, "Key 必须是整型，用于范围循环");

public:
    explicit CacheBenchmark(CachePolicy<Key,Value>& policy,
                            size_t thread_count)
      : policy_(policy), thread_count_(thread_count)
    {}

    // -------- 随机模式 --------
    // get_ratio: GET 操作占比
    void runRandomPattern(size_t key_range,
                          size_t total_ops,
                          double get_ratio = 0.8)
    {
        auto worker = [&](size_t tid, CacheStats<Key,Value>& stats, std::vector<double>& times){
            // 每线程独享 RNG
            std::mt19937_64 rnd{ std::random_device{}() ^ tid };
            std::uniform_int_distribution<Key> key_dist{ 0, Key(key_range - 1) };
            std::uniform_real_distribution<> op_dist{ 0.0, 1.0 };

            size_t ops_per_thread = total_ops / thread_count_;
            stats.total_ops = ops_per_thread;
            times.reserve(ops_per_thread);

            for (size_t i = 0; i < ops_per_thread; ++i) {
                Key k      = key_dist(rnd);
                bool is_get= op_dist(rnd) < get_ratio;

                auto t0 = clk::now();
                if (is_get) {
                    ++stats.total_get;
                    Value out{};
                    if (policy_.get(k, out)) ++stats.cache_hits;
                    else                      ++stats.cache_misses;
                } else {
                    ++stats.total_put;
                    policy_.put(k, Value{});
                }
                auto t1 = clk::now();
                times.push_back(ns(t0,t1));
            }
        };

        auto total = runBenchmark(worker, total_ops);
        printRandomReport(total, key_range, get_ratio);
    }

    // -------- 混合模式 --------
    // scan_range: A/C 阶段扫描范围
    // hotspot_range: B 阶段热点 key 范围
    // hotspot_accesses: B 阶段访问次数
    // put_ratio: B 阶段 put 占比（新增参数）
    void runMixedPattern(size_t scan_range,
                         size_t hotspot_range,
                         size_t hotspot_accesses,
                         double put_ratio = 0.0)
    {
        auto worker = [&](size_t tid, CacheStats<Key,Value>& stats, std::vector<double>& times){
            std::mt19937_64 rnd{ std::random_device{}() ^ tid };
            std::uniform_int_distribution<Key> hot_dist{ 0, Key(hotspot_range - 1) };
            size_t ops = scan_range * 2 + hotspot_accesses;
            stats.total_ops = ops;
            times.reserve(ops);

            // A: 全扫描
            for (Key k = 0; k < Key(scan_range); ++k)
                measureGet(k, stats, times);

            // B: 热点访问（新增 PUT 支持）
            for (size_t i = 0; i < hotspot_accesses; ++i) {
                Key k   = hot_dist(rnd);
                bool is_put = (put_ratio > 0.0) && (std::uniform_real_distribution<>(0.,1.)(rnd) < put_ratio);
                if (is_put) {
                    measurePut(k, stats, times);
                } else {
                    measureGet(k, stats, times);
                }
            }

            // C: 再次全扫描
            for (Key k = 0; k < Key(scan_range); ++k)
                measureGet(k, stats, times);
        };

        // explicit_total_ops 已在 worker 里写入 stats.total_ops，无需传参
        auto total = runBenchmark(worker);
        printMixedReport(total, scan_range, hotspot_range, hotspot_accesses, put_ratio);
    }

private:
    using clk = std::chrono::high_resolution_clock;

    // GET/PUT 封装，自动记录时间和 stats
    void measureGet(const Key& k, CacheStats<Key,Value>& st, std::vector<double>& ts) {
        auto t0 = clk::now();
        Value out{};
        if (policy_.get(k, out)) ++st.cache_hits;
        else                      ++st.cache_misses;
        auto t1 = clk::now();
        ++st.total_get;
        ts.push_back(ns(t0,t1));
    }

    void measurePut(const Key& k, CacheStats<Key,Value>& st, std::vector<double>& ts) {
        auto t0 = clk::now();
        policy_.put(k, Value{});
        auto t1 = clk::now();
        ++st.total_put;
        ts.push_back(ns(t0,t1));
    }

    static double ns(const std::chrono::time_point<clk>& a,
                     const std::chrono::time_point<clk>& b)
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    }

    /// @param explicit_total_ops: 如果非 0，每线程 ops 从 worker 外部分配；否则用 stats.total_ops
    template<typename Worker>
    CacheStats<Key,Value> runBenchmark(Worker&& worker,
                                       size_t explicit_total_ops = 0)
    {
        std::vector<std::thread> threads;
        std::vector<CacheStats<Key,Value>> stats(thread_count_);
        std::vector<std::vector<double>>    times(thread_count_);

        auto t_begin = clk::now();
        for (size_t t = 0; t < thread_count_; ++t) {
            threads.emplace_back(
                [&, t]() {
                    // 如果显式传 total_ops，则先给 stats.total_ops
                    if (explicit_total_ops) {
                        stats[t].total_ops = explicit_total_ops / thread_count_;
                    }
                    worker(t, stats[t], times[t]);
                }
            );
        }
        for (auto& th : threads) th.join();
        auto t_end = clk::now();

        // 预分配 all_ns
        CacheStats<Key,Value> total;
        size_t N = 0;
        for (auto& st : stats) N += st.total_ops;
        std::vector<double> all_ns; all_ns.reserve(N);

        // 合并
        size_t sum_hits = 0, sum_misses = 0;
        for (size_t t = 0; t < thread_count_; ++t) {
            auto& st = stats[t];
            total.total_ops    += st.total_ops;
            total.total_get    += st.total_get;
            total.total_put    += st.total_put;
            sum_hits           += st.cache_hits;
            sum_misses         += st.cache_misses;
            all_ns.insert(all_ns.end(),
                          times[t].begin(), times[t].end());
        }
        total.cache_hits   = sum_hits;
        total.cache_misses = sum_misses;
        total.hit_rate     = 100.0 * double(sum_hits)   / total.total_ops;
        total.miss_rate    = 100.0 * double(sum_misses) / total.total_ops;

        total.elapsed_ms   = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_begin).count() / 1000.0;
        total.ops_per_sec  = total.total_ops / (total.elapsed_ms / 1000.0);

        // 平均 & 标准差
        double sum = 0;
        for (auto x : all_ns) sum += x;
        total.avg_access_time = sum / all_ns.size();

        double var = 0;
        for (auto x : all_ns) var += (x - total.avg_access_time)*(x - total.avg_access_time);
        total.stddev_ns = std::sqrt(var / all_ns.size());

        return total;
    }

    // -------- 打印部分 --------

    void printRandomReport(const CacheStats<Key,Value>& st,
                           size_t key_range, double get_ratio)
    {
        std::cout << "\n===== 随机模式性能报告 (threads="<< thread_count_ <<") =====\n";
        std::cout << "键范围: [0," << key_range << ")  GET比例: " << (get_ratio*100) << "%\n";
        printCommonReport(st);
    }

    void printMixedReport(const CacheStats<Key,Value>& st,
                          size_t scan_range, size_t hotspot_range, size_t hotspot_accesses,
                          double put_ratio)
    {
        std::cout << "\n===== 混合模式性能报告 (threads="<< thread_count_ <<") =====\n";
        std::cout << "阶段 A/C: [0,"<< scan_range <<")  阶段 B: [0,"<< hotspot_range
                  <<") x"<< hotspot_accesses
                  << "  B中PUT比例: " << (put_ratio*100) << "%\n";
        printCommonReport(st);
    }

    void printCommonReport(const CacheStats<Key,Value>& st) {
        std::cout << "总操作数:     " << st.total_ops                  << "\n"
                  << "GET:          " << st.total_get 
                  << " (" << std::fixed << std::setprecision(1) 
                  << (100.0*st.total_get/st.total_ops) << "%)\n"
                  << "PUT:          " << st.total_put 
                  << " (" << std::fixed << std::setprecision(1) 
                  << (100.0*st.total_put/st.total_ops) << "%)\n"
                  << "命中:         " << st.cache_hits 
                  << " ("<< std::fixed << std::setprecision(2)<< st.hit_rate <<"%)\n"
                  << "未命中:       " << st.cache_misses 
                  << " ("<< std::fixed << std::setprecision(2)<< st.miss_rate<<"%)\n"
                  << "总耗时:       " << std::fixed<< std::setprecision(2)<< st.elapsed_ms  <<" ms\n"
                  << "吞吐量:       " << std::fixed<< std::setprecision(2)<< st.ops_per_sec <<" ops/s\n"
                  << "平均访问:     " << std::fixed<< std::setprecision(2)<< st.avg_access_time <<" ns\n"
                  << "访问标准差:   " << std::fixed<< std::setprecision(2)<< st.stddev_ns       <<" ns\n"
                  << "===============================================\n";
    }

private:
    CachePolicy<Key,Value>& policy_;
    size_t                  thread_count_;
};

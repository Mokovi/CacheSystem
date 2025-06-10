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
#include <functional>   // for std::function
#include <utility>      // for std::pair
#include <string>       // for std::string
#include "CachePolicy.hpp"

template<typename Key, typename Value>
struct CacheStats {
    size_t total_ops        = 0;
    size_t total_get        = 0;
    size_t total_put        = 0;
    size_t cache_hits       = 0;
    size_t cache_misses     = 0;
    double hit_rate         = 0.0;   // %
    double ops_per_sec      = 0.0;   // ops/s
    double avg_access_time  = 0.0;   // ns
    double stddev_ns        = 0.0;   // ns
    double elapsed_ms       = 0.0;   // ms
};

template<typename Key, typename Value>
class CacheBenchmark {
    static_assert(std::is_integral<Key>::value, "Key 必须是整型");

public:
    explicit CacheBenchmark(CachePolicy<Key,Value>& policy, size_t thread_count)
      : policy_(policy), threads_(thread_count)
    {}

    CacheStats<Key,Value> runRandomPatternStats(size_t key_range,
                                                size_t total_ops,
                                                double get_ratio = 0.8)
    {
        auto worker = [&](size_t tid, CacheStats<Key,Value>& st, std::vector<double>& ts){
            std::mt19937_64 rnd{ std::random_device{}() ^ tid };
            std::uniform_int_distribution<Key> key_dist{ 0, Key(key_range-1) };
            std::uniform_real_distribution<> op_dist{ 0.0, 1.0 };

            size_t ops_thr = total_ops / threads_;
            st.total_ops = ops_thr;
            ts.reserve(ops_thr);

            for (size_t i = 0; i < ops_thr; ++i) {
                Key k = key_dist(rnd);
                bool is_get = (op_dist(rnd) < get_ratio);

                auto t0 = clk::now();
                if (is_get) {
                    ++st.total_get;
                    Value out{};
                    if (policy_.get(k, out)) ++st.cache_hits;
                    else                      ++st.cache_misses;
                } else {
                    ++st.total_put;
                    policy_.put(k, Value{});
                }
                auto t1 = clk::now();
                ts.push_back(ns(t0,t1));
            }
        };

        return runBenchmark(worker, total_ops);
    }

    CacheStats<Key,Value> runMixedPatternStats(size_t scan_range,
                                               size_t hotspot_range,
                                               size_t hotspot_accesses,
                                               double put_ratio = 0.0)
    {
        auto worker = [&](size_t tid, CacheStats<Key,Value>& st, std::vector<double>& ts){
            std::mt19937_64 rnd{ std::random_device{}() ^ tid };
            std::uniform_int_distribution<Key> hot_dist{ 0, Key(hotspot_range-1) };
            size_t ops = scan_range*2 + hotspot_accesses;
            st.total_ops = ops;
            ts.reserve(ops);

            // A: 全扫描
            for (Key k = 0; k < Key(scan_range); ++k)
                measureGet(k, st, ts);

            // B: 热点访问（可 PUT）
            for (size_t i = 0; i < hotspot_accesses; ++i) {
                Key k = hot_dist(rnd);
                bool is_put = (put_ratio > 0 && std::uniform_real_distribution<>(0,1)(rnd) < put_ratio);
                if (is_put) measurePut(k, st, ts);
                else        measureGet(k, st, ts);
            }

            // C: 再次全扫描
            for (Key k = 0; k < Key(scan_range); ++k)
                measureGet(k, st, ts);
        };

        return runBenchmark(worker);
    }

private:
    using clk = std::chrono::high_resolution_clock;

    static double ns(const std::chrono::time_point<clk>& a,
                     const std::chrono::time_point<clk>& b)
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    }

    void measureGet(const Key& k, CacheStats<Key,Value>& st, std::vector<double>& ts) {
        auto t0 = clk::now();
        Value out{};
        if (policy_.get(k, out)) ++st.cache_hits;
        else                      ++st.cache_misses;
        ++st.total_get;
        auto t1 = clk::now();
        ts.push_back(ns(t0,t1));
    }

    void measurePut(const Key& k, CacheStats<Key,Value>& st, std::vector<double>& ts) {
        auto t0 = clk::now();
        policy_.put(k, Value{});
        ++st.total_put;
        auto t1 = clk::now();
        ts.push_back(ns(t0,t1));
    }

    template<typename Worker>
    CacheStats<Key,Value> runBenchmark(Worker&& worker,
                                       size_t explicit_total_ops = 0)
    {
        std::vector<std::thread> threads;
        std::vector<CacheStats<Key,Value>> stats(threads_);
        std::vector<std::vector<double>>    times(threads_);

        auto t_begin = clk::now();
        for (size_t t = 0; t < threads_; ++t) {
            threads.emplace_back([&, t]() {
                if (explicit_total_ops)
                    stats[t].total_ops = explicit_total_ops / threads_;
                worker(t, stats[t], times[t]);
            });
        }
        for (auto& th : threads) th.join();
        auto t_end = clk::now();

        CacheStats<Key,Value> total;
        std::vector<double> all_ns;
        all_ns.reserve(explicit_total_ops ? explicit_total_ops
                          : std::accumulate(stats.begin(), stats.end(), size_t(0),
                            [](size_t a, auto& s){ return a + s.total_ops; }));

        size_t sum_hits = 0, sum_misses = 0;
        for (size_t i = 0; i < threads_; ++i) {
            auto& st = stats[i];
            total.total_ops    += st.total_ops;
            total.total_get    += st.total_get;
            total.total_put    += st.total_put;
            sum_hits           += st.cache_hits;
            sum_misses         += st.cache_misses;
            all_ns.insert(all_ns.end(), times[i].begin(), times[i].end());
        }

        total.cache_hits   = sum_hits;
        total.cache_misses = sum_misses;
        total.hit_rate     = 100.0 * double(sum_hits) / total.total_ops;
        total.elapsed_ms   = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_begin).count() / 1000.0;
        total.ops_per_sec  = total.total_ops / (total.elapsed_ms / 1000.0);

        double sum = std::accumulate(all_ns.begin(), all_ns.end(), 0.0);
        total.avg_access_time = sum / all_ns.size();
        double var = 0;
        for (auto x : all_ns) var += (x - total.avg_access_time)*(x - total.avg_access_time);
        total.stddev_ns = std::sqrt(var / all_ns.size());

        return total;
    }

    CachePolicy<Key,Value>& policy_;
    size_t                   threads_;
};



// -----------------------------------------------------------------------------
// CacheBenchmarkSuite: 批量对比多个策略的性能 & 命中率
// -----------------------------------------------------------------------------
template<typename Key, typename Value>
class CacheBenchmarkSuite {
public:
    using Factory = std::function<std::unique_ptr<CachePolicy<Key,Value>>()>;

    void addPolicy(const std::string& name, Factory f) {
        policies_.emplace_back(name, std::move(f));
    }

    void runRandomAll(size_t key_range,
                      size_t total_ops,
                      double get_ratio,
                      size_t thread_count)
    {
        std::cout << "\n=== 随机模式对比 (keys=[0,"<<key_range<<")  ops="<<total_ops
                  << "  GET%="<< (get_ratio*100) <<"  threads="<<thread_count<<" ===\n";
        std::cout << std::left << std::setw(20) << "Policy"
                  << std::right<< std::setw(12) << "HitRate(%)"
                  << std::setw(14) << "Ops/s"
                  << std::setw(14) << "Avg(ns)"
                  << "\n" << std::string(60,'-') << "\n";

        for (auto& pf : policies_) {
            const auto& name    = pf.first;
            const auto& factory = pf.second;
            auto policy = factory();
            CacheBenchmark<Key,Value> bench(*policy, thread_count);
            auto st = bench.runRandomPatternStats(key_range, total_ops, get_ratio);
            std::cout << std::left << std::setw(20) << name
                      << std::right<< std::setw(12)<< std::fixed<<std::setprecision(2)<<st.hit_rate
                      << std::setw(14)<<st.ops_per_sec
                      << std::setw(14)<<st.avg_access_time
                      << "\n";
        }
        std::cout << std::string(60,'=') << "\n";
    }

    void runMixedAll(size_t scan_range,
                     size_t hotspot_range,
                     size_t hotspot_accesses,
                     double put_ratio,
                     size_t thread_count)
    {
        std::cout << "\n=== 混合模式对比 (scan="<<scan_range
                  << "  hot=[0,"<<hotspot_range<<") x"<<hotspot_accesses
                  << "  PUT%="<<(put_ratio*100)
                  <<"  threads="<<thread_count<<" ===\n";
        std::cout << std::left << std::setw(20) << "Policy"
                  << std::right<< std::setw(12) << "HitRate(%)"
                  << std::setw(14) << "Ops/s"
                  << std::setw(14) << "Avg(ns)"
                  << "\n" << std::string(60,'-') << "\n";

        for (auto& pf : policies_) {
            const auto& name    = pf.first;
            const auto& factory = pf.second;
            auto policy = factory();
            CacheBenchmark<Key,Value> bench(*policy, thread_count);
            auto st = bench.runMixedPatternStats(scan_range, hotspot_range, hotspot_accesses, put_ratio);
            std::cout << std::left << std::setw(20) << name
                      << std::right<< std::setw(12)<<std::fixed<<std::setprecision(2)<<st.hit_rate
                      << std::setw(14)<<st.ops_per_sec
                      << std::setw(14)<<st.avg_access_time
                      << "\n";
        }
        std::cout << std::string(60,'=') << "\n";
    }

private:
    std::vector<std::pair<std::string, Factory>> policies_;
};

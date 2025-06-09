#include "testLru.hpp"
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <iomanip>
#include <mutex>
#include <cmath>

// 线程工作函数
void thread_worker(LruCache<int, std::string>& cache, 
                   PerformanceStats& stats,
                   int thread_id,
                   std::atomic<long>& global_hits,
                   std::atomic<long>& global_misses) {
    std::mt19937 gen(thread_id); // 每个线程独立的随机数生成器

    long local_gets = 0;
    long local_puts = 0;
    long local_hits = 0;
    long local_misses = 0;

    // 随机键生成器
    auto random_key = [&gen]() {
        std::uniform_int_distribution<> dist(0, KEY_RANGE - 1);
        return dist(gen);
    };
    
    // 随机值生成器
    auto random_value = [&gen]() {
        std::uniform_int_distribution<> char_dist('a', 'z');
        std::string value(20, ' ');  // 生成20字节的值
        for (char& c : value) {
            c = static_cast<char>(char_dist(gen));
        }
        return value;
    };

    for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
        int key = random_key();
        
        // 80% get操作 / 20% put操作
        if (gen() % 100 < 80) {
            std::string value;
            auto start = std::chrono::high_resolution_clock::now();
            bool hit = cache.get(key, value);
            auto end = std::chrono::high_resolution_clock::now();
            
            if (hit) {
                local_hits++;
                global_hits.fetch_add(1, std::memory_order_relaxed);
            } else {
                local_misses++;
                global_misses.fetch_add(1, std::memory_order_relaxed);
            }
            
            // 累加访问时间（纳秒）
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            stats.avg_access_time += duration;
            local_gets++;
        } else {
            auto start = std::chrono::high_resolution_clock::now();
            cache.put(key, random_value());
            auto end = std::chrono::high_resolution_clock::now();
            
            // 累加访问时间（纳秒）
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            stats.avg_access_time += duration;
            local_puts++;
        }
    }

    stats.total_get = local_gets;
    stats.total_put = local_puts;
    stats.cache_hits = local_hits;
    stats.cache_misses = local_misses;
    stats.total_ops = OPERATIONS_PER_THREAD;
    
    // 计算线程平均访问时间
    if (stats.total_ops > 0) {
        stats.avg_access_time /= stats.total_ops;
    }
}

// 主性能测试函数
void testLruCachePerformance() {
    
    // 全局命中/未命中计数器
    std::atomic<long> global_hits(0);
    std::atomic<long> global_misses(0);
    
    for (int thread_count : THREAD_COUNTS) {
        global_hits = 0;
        global_misses = 0;
        
        LruCache<int, std::string> cache(TEST_CAPACITY);
        std::vector<std::thread> threads;
        std::vector<PerformanceStats> thread_stats(thread_count);
        
        auto start_time = std::chrono::high_resolution_clock::now();

        // 启动工作线程
        for (int i = 0; i < thread_count; ++i) {
            threads.emplace_back(thread_worker, 
                                std::ref(cache), 
                                std::ref(thread_stats[i]),
                                i,
                                std::ref(global_hits),
                                std::ref(global_misses));
        }

        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();

        // 汇总统计结果
        PerformanceStats total_stats{0, 0, 0, elapsed_ms};
        double total_access_time = 0.0;
        
        for (auto& stat : thread_stats) {
            total_stats.total_ops += stat.total_ops;
            total_stats.total_get += stat.total_get;
            total_stats.total_put += stat.total_put;
            total_stats.cache_hits += stat.cache_hits;
            total_stats.cache_misses += stat.cache_misses;
            total_access_time += stat.avg_access_time * stat.total_ops;
        }
        
        // 计算全局指标
        total_stats.ops_per_sec = (total_stats.total_ops / elapsed_ms) * 1000;
        total_stats.hit_rate = (total_stats.total_get > 0) ? 
            (100.0 * total_stats.cache_hits / total_stats.total_get) : 0.0;
        total_stats.miss_rate = (total_stats.total_get > 0) ? 
            (100.0 * total_stats.cache_misses / total_stats.total_get) : 0.0;
        total_stats.avg_access_time = (total_stats.total_ops > 0) ? 
            (total_access_time / total_stats.total_ops) : 0.0;
        
        // 计算标准差（衡量访问时间稳定性）
        double variance = 0.0;
        for (auto& stat : thread_stats) {
            for (int i = 0; i < stat.total_ops; i++) {
                double diff = stat.avg_access_time - total_stats.avg_access_time;
                variance += diff * diff;
            }
        }
        double std_dev = (total_stats.total_ops > 0) ? 
            std::sqrt(variance / total_stats.total_ops) : 0.0;

        // 输出性能报告
        std::cout << "\n===== 性能测试报告 (线程数: " << thread_count << ") =====" << std::endl;
        std::cout << "缓存容量: \t" << TEST_CAPACITY << std::endl;
        std::cout << "键范围: \t" << KEY_RANGE << " (预期命中率: ~50%)" << std::endl;
        std::cout << "总操作数: \t" << total_stats.total_ops << std::endl;
        std::cout << "GET操作: \t" << total_stats.total_get << " (" 
                 << std::fixed << std::setprecision(1) 
                 << (100.0 * total_stats.total_get / total_stats.total_ops) << "%)" << std::endl;
        std::cout << "PUT操作: \t" << total_stats.total_put << " (" 
                 << std::fixed << std::setprecision(1) 
                 << (100.0 * total_stats.total_put / total_stats.total_ops) << "%)" << std::endl;
        std::cout << "缓存命中: \t" << total_stats.cache_hits << " ("
                 << std::fixed << std::setprecision(2) << total_stats.hit_rate << "%)" << std::endl;
        std::cout << "缓存未命中: \t" << total_stats.cache_misses << " ("
                 << std::fixed << std::setprecision(2) << total_stats.miss_rate << "%)" << std::endl;
        std::cout << "总耗时: \t" << std::fixed << std::setprecision(2) << elapsed_ms << " ms" << std::endl;
        std::cout << "吞吐量: \t" << std::fixed << std::setprecision(2) << total_stats.ops_per_sec 
                 << " 操作/秒" << std::endl;
        std::cout << "平均访问时间: \t" << std::fixed << std::setprecision(2) << total_stats.avg_access_time 
                 << " ns" << std::endl;
        std::cout << "访问时间标准差: " << std::fixed << std::setprecision(2) << std_dev 
                 << " ns" << std::endl;
        std::cout << "=====================================\n";
    }
}

void testLruCacheFeature(){
    // 创建一个容量为 3 的 LRU 缓存，Key 为 string，Value 为 int
    LruCache<std::string, int> cache(3);

    // put 操作
    cache.put("one", 1);
    cache.put("two", 2);
    cache.put("three", 3);

    // 访问"one"，使其成为最近使用
    {
        int val;
        if (cache.get("one", val)) {
            std::cout << "get(one) = " << val << "\n";  // 应输出 1
        } else {
            std::cout << "one 未命中\n";
        }
    }

    // 再插入第四个元素，触发驱逐最旧条目
    // 当前链表顺序（最旧→最近）: two -> three -> one
    cache.put("four", 4);
    // 此时应驱逐"two"，因为它是最旧未访问的

    // 验证"two"是否被驱逐
    {
        int val;
        bool ok = cache.get("two", val);
        std::cout << "二次 get(two)：";
        if (!ok) {
            std::cout << "未命中（已被驱逐）\n";
        } else {
            std::cout << "命中! val = " << val << "\n";
        }
    }

    // 打印其余三个键的值，验证它们依然存在
    for (auto key : { "one", "three", "four" }) {
        int val;
        if (cache.get(key, val)) {
            std::cout << "get(" << key << ") = " << val << "\n";
        } else {
            std::cout << key << " 未命中\n";
        }
    }

    // 再次插入一个新键 "five"，此时链表（最旧→最近）应为:
    // three -> one -> four
    // 插入 five 会驱逐最旧"three"
    cache.put("five", 5);

    // 验证"three"是否被驱逐
    {
        int val;
        bool ok = cache.get("three", val);
        std::cout << "再一次 get(three)：";
        if (!ok) {
            std::cout << "未命中（已被驱逐）\n";
        } else {
            std::cout << "命中! val = " << val << "\n";
        }
    }

    // 最终缓存应包括： one, four, five（访问顺序从最旧到最近）
    std::cout << "最终缓存内容：\n";
    for (auto key : { "one", "four", "five" }) {
        int val;
        cache.get(key, val);  // get 会把 key 移到最近，但这里只是拿值
        std::cout << "  " << key << " = " << val << "\n";
    }
}
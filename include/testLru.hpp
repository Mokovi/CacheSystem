#ifndef __TESTLRU__HPP___
#define __TESTLRU__HPP___

#include <iostream>
#include "LruCache.hpp"

// 性能测试结果结构体
struct PerformanceStats {
    // 基础指标
    long total_ops;
    long total_get;
    long total_put;
    double elapsed_ms;
    double ops_per_sec;
    // 缓存相关指标
    long cache_hits;
    long cache_misses;
    double hit_rate;        // 命中率
    double miss_rate;       // 未命中率
    double avg_access_time; // 平均访问时间 (纳秒)
};

const int KEY_RANGE = 2000;  // 键值范围
const int OPERATIONS_PER_THREAD = 100000; // 每个线程操作次数
const int TEST_CAPACITY = 1000;      // 缓存容量
const int THREAD_COUNTS[] = {1, 2, 4, 8}; // 测试的线程数


/// @brief 测试性能并输出测试报告
void testLruCachePerformance();

/// @brief 测试主要功能是否实现，用与Debug时期
void testLruCacheFeature();

#endif
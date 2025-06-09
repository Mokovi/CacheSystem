#include "CacheBenchmark.hpp"
#include "LruCache.hpp"
#include "LruKCache.hpp"
#include "HashLruKCache.hpp"

int main() {
    constexpr size_t THREADS    = 8;
    constexpr size_t KEY_RANGE  = 1000;
    constexpr size_t TOTAL_OPS  = 1'000'000;
    constexpr size_t SCAN_RANGE        = 5000;  // 全量扫描范围 M
    constexpr size_t HOTSPOT_RANGE     = 500;   // 热点区间 H
    constexpr size_t HOTSPOT_ACCESSES  = 20000; // 热点访问次数

    // 1) LRU
    LruCache<int,int> lru(160);
    CacheBenchmark<int,int> bench1(lru, THREADS);

    // 2) LRU-K (K=2)
    LruKCache<int,int> lruk(2, 160, 160);
    CacheBenchmark<int,int> bench2(lruk, THREADS);

    // 3) Hash-LRU-K
    HashLruKCache<int,int,16> hash_lruk(2, 10, 10);
    CacheBenchmark<int,int> bench3(hash_lruk, THREADS);

    bench1.runRandomPattern(KEY_RANGE, TOTAL_OPS);
    bench2.runRandomPattern(KEY_RANGE, TOTAL_OPS);
    bench3.runRandomPattern(KEY_RANGE, TOTAL_OPS);
    bench1.runMixedPattern(SCAN_RANGE, HOTSPOT_RANGE, HOTSPOT_ACCESSES);
    bench2.runMixedPattern(SCAN_RANGE, HOTSPOT_RANGE, HOTSPOT_ACCESSES);
    bench3.runMixedPattern(SCAN_RANGE, HOTSPOT_RANGE, HOTSPOT_ACCESSES);

    return 0;
}

#include "CacheBenchmark.hpp"
#include "LfuCache.hpp"
#include "LfuAgingCache.hpp"
#include "HashLfuAgingCache.hpp"
#include "LruCache.hpp"
#include "LruKCache.hpp"
#include "HashLruKCache.hpp"
#include "ARCache.hpp"

int main() {
    CacheBenchmarkSuite<int,int> suite;

    // 注册各个策略
    suite.addPolicy("LFU", []{ return std::make_unique<LfuCache<int,int>>(10000); });
    suite.addPolicy("LFU-Aging", []{ return std::make_unique<LfuAgingCache<int,int>>(10000); });
    suite.addPolicy("HashLFU-Aging", []{ return std::make_unique<HashLfuAgingCache<int,int>>(10000, 8); });
    suite.addPolicy("LRU", []{ return std::make_unique<LruCache<int,int>>(10000); });
    suite.addPolicy("LRU-K", []{ return std::make_unique<LruKCache<int,int>>(2,1000,1000); });
    suite.addPolicy("HashLRU-K", []{ return std::make_unique<HashLruKCache<int,int, 8>>(2,125,125); });
    suite.addPolicy("ARC",[](){return std::make_unique<ARCCache<int,int>>(1000); });

    // 随机模式对比
    suite.runRandomAll(
        /*key_range=*/10000,
        /*total_ops=*/200000,
        /*get_ratio=*/0.8,
        /*thread_count=*/4
    );

    // 混合模式对比
    suite.runMixedAll(
        /*scan_range=*/5000,
        /*hotspot_range=*/1000,
        /*hotspot_accesses=*/50000,
        /*put_ratio=*/0.1,
        /*thread_count=*/4
    );

    return 0;
}

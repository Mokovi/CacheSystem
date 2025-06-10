#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include "LfuAgingCache.hpp"

template<typename Key, typename Value>
class HashLfuAgingCache : public CachePolicy<Key, Value> {
public:
    /**
     * @param totalCapacity   整个缓存的总容量
     * @param shardCount      分片数量（建议与 CPU 核数或其倍数）
     * @param maxAvgFreqLimit Aging 触发阈值
     */
    HashLfuAgingCache(size_t totalCapacity,
                      size_t shardCount,
                      double maxAvgFreqLimit = 10.0)
        : shardCount_(shardCount)
    {
        size_t base = totalCapacity / shardCount;
        size_t rem  = totalCapacity % shardCount;
        shards_.reserve(shardCount_);
        for (size_t i = 0; i < shardCount_; ++i) {
            // 最后一个分区多一些
            size_t cap = base + (i + 1 == shardCount_ ? rem : 0);
            shards_.emplace_back(cap, maxAvgFreqLimit);
        }
    }

    void put(const Key& key, const Value& value) override {
        auto& shard = getShard(key);
        std::lock_guard<std::mutex> guard(shard.mtx);
        shard.cache.put(key, value);
    }

    bool get(const Key& key, Value& value) override {
        auto& shard = getShard(key);
        std::lock_guard<std::mutex> guard(shard.mtx);
        return shard.cache.get(key, value);
    }

    Value get(const Key& key) override {
        Value tmp;
        return get(key, tmp) ? tmp : Value{};
    }

    void remove(const Key& key) override {
        auto& shard = getShard(key);
        std::lock_guard<std::mutex> guard(shard.mtx);
        shard.cache.remove(key);
    }

    void removeAll() override {
        for (auto& shard : shards_) {
            std::lock_guard<std::mutex> guard(shard.mtx);
            shard.cache.removeAll();
        }
    }

private:
    struct Shard {
        LfuAgingCache<Key, Value> cache;
        std::mutex                mtx;
        Shard(size_t capacity, double avgLimit)
            : cache(int(capacity), avgLimit)
        {}
    };

    Shard& getShard(const Key& key) {
        size_t h = hasher_(key);
        return shards_[h % shardCount_];
    }

    size_t                     shardCount_;
    std::vector<Shard>         shards_;
    std::hash<Key>             hasher_;
};

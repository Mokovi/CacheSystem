#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <cassert>
#include "CachePolicy.hpp"
#include "LruKCache.hpp"

template<
    typename Key,
    typename Value,
    size_t NumShards = 16
>
class HashLruKCache : public CachePolicy<Key,Value> {
    static_assert(NumShards >= 1, "NumShards must be >= 1");
    // 如果你想用位运算优化 h % NumShards，请确保 NumShards 是 2 的幂：
    static_assert((NumShards & (NumShards - 1)) == 0,
                  "NumShards should be a power of two for optimal hashing");

public:
    HashLruKCache(int k, int historyCapacity, int mainCacheCapacity) {
        assert(k > 0);
        shards_.reserve(NumShards);
        for (size_t i = 0; i < NumShards; ++i) {
            shards_.push_back(
                std::make_unique<Shard>(k, historyCapacity, mainCacheCapacity)
            );
        }
    }

    // 禁用拷贝，支持移动
    HashLruKCache(const HashLruKCache&) = delete;
    HashLruKCache& operator=(const HashLruKCache&) = delete;
    HashLruKCache(HashLruKCache&&) noexcept = default;
    HashLruKCache& operator=(HashLruKCache&&) noexcept = default;

    // --- CachePolicy 接口 ---
    void put(const Key& key, const Value& value) override {
        auto& shard = *getShardPtr(key);
        std::lock_guard<std::mutex> lk(shard.mtx);
        shard.cache.put(key, value);
    }

    bool get(const Key& key, Value& value) override {
        auto& shard = *getShardPtr(key);
        std::lock_guard<std::mutex> lk(shard.mtx);
        return shard.cache.get(key, value);
    }

    Value get(const Key& key) override {
        Value tmp{};
        get(key, tmp);
        return tmp;
    }

    void remove(const Key& key) override {
        auto& shard = *getShardPtr(key);
        std::lock_guard<std::mutex> lk(shard.mtx);
        shard.cache.remove(key);
    }

    void removeAll() override {
        for (auto& ptr : shards_) {
            std::lock_guard<std::mutex> lk(ptr->mtx);
            ptr->cache.removeAll();
        }
    }
    // --- end CachePolicy 接口 ---

private:
    struct Shard {
        Shard(int k, int histCap, int mainCap)
          : cache(k, histCap, mainCap)
        {}
        LruKCache<Key, Value> cache;
        std::mutex            mtx;
    };

    // 得到指向对应 shard 的指针
    Shard* getShardPtr(const Key& key) const {
        size_t h = hasher_(key);
        // 如果 NumShards 是 2 的幂，可写成 (h & (NumShards - 1))
        return shards_[h % NumShards].get();
    }

private:
    std::vector<std::unique_ptr<Shard>> shards_;
    std::hash<Key>                      hasher_;
};

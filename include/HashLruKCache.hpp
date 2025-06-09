#pragma once
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include "LruKCache.hpp"

/*
    NumShards 放在模版里面比放在类里面更好吗？ 更推荐放在哪？
        
    放在模板参数：
        • 编译期常量，取模运算可以编译器优化（尤其是2的幂可变成位运算）。
        • 不占对象内存。
        • 代码更“静态”，一旦编译就固定不变。
        • 每实例化一个不同的 NumShards 都会生成一份新的类代码，可能增加二进制体积。
        • 不太方便在运行时调整。	
    放在类成员：
        • 运行期可配，对象创建时能动态指定分片数。
        • 对应的 vector 容器可以按实际需求大小分配。
        • 更灵活，适合不同场景不同实例用不同 shard 数。
        • 一份类模板／类就能支持所有分片数，二进制更精简。
        • 如果分片数是常量，也可以在构造里用位运算优化。
    总结：
        均可，区别不大。
*/

template<typename Key, typename Value, size_t NumShards = 16>
class HashLruKCache : public CachePolicy {
public:
    /// @brief 算法构造函数
    /// @param k 数据存入主缓存访问次数设定
    /// @param historyCapacity 分区历史记录容量
    /// @param mainCacheCapacity 分区缓存容量
    HashLruKCache(int k, int historyCapacity, int mainCacheCapacity) {
        shards_.reserve(NumShards);
        for (size_t i = 0; i < NumShards; ++i) {
            shards_.emplace_back(
                k, historyCapacity, mainCacheCapacity
            );
        }
    }

    /// @brief 更新/添加缓存数据
    /// @param key 键
    /// @param value 值
    void put(const Key& key, const Value& value) override {
        auto& shard = getShard(key);
        std::lock_guard<std::mutex> lk(shard.mtx);
        shard.cache.put(key, value);
    }

    /// @brief 获取缓存值，命中返回true,否则false
    /// @param key 键
    /// @param value 值
    /// @return 从主缓存中取到值则返回True,同时修改传参value;不在缓存中则返回false.
    bool get(const Key& key, Value& value) override {
        auto& shard = getShard(key);
        std::lock_guard<std::mutex> lk(shard.mtx);
        return shard.cache.get(key, value);
    }

    /// @brief 删除特定缓存
    /// @param key 键
    void remove(const Key& key) override {
        auto& shard = getShard(key);
        std::lock_guard<std::mutex> lk(shard.mtx);
        shard.cache.remove(key);
    }

    /// @brief 清空缓存
    void removeAll() override {
        for (auto& shard : shards_) {
            std::lock_guard<std::mutex> lk(shard.mtx);
            shard.cache.removeAll();
        }
    }

private:
    struct Shard {
        Shard(int k, int histCap, int mainCap)
          : cache(k, histCap, mainCap)
        {}
        LruKCache<Key, Value> cache;
        std::mutex            mtx;
    };

    // 根据 key 计算落在哪个 shard
    Shard& getShard(const Key& key) {
        size_t h = hasher_(key);
        // 如果 NumShards 是 2 的幂，这里可以用位运算 h & (NumShards-1) 更高效
        return shards_[h % NumShards];
    }

    std::vector<Shard>         shards_;
    std::hash<Key>             hasher_;
};

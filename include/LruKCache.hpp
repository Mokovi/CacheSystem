#pragma once

#include "LruCache.hpp"

template<class Key, class Value>
class LruKCache : public LruCache<Key, Value> {
public:
    /// @brief LRU-K缓存算法构造函数
    /// @param k 数据存入主缓存访问次数设定
    /// @param historyCapacity 历史数据容量
    /// @param mainCapacity 主缓存容量
    LruKCache(int k, int historyCapacity, int mainCapacity)
      : LruCache<Key,Value>(mainCapacity)
      , k_(k)
      , history_(historyCapacity) 
    {
        assert(k > 0);
    }

    /// @brief 更新/添加缓存数据
    /// @param key 键
    /// @param value 值
    void put(const Key& key, const Value& value) override {
        std::lock_guard<std::mutex> lk(mtx_);
        Value dummy{};
        // 如果已在主缓存，直接更新并返回
        if (LruCache<Key,Value>::get(key, dummy)) {
            LruCache<Key,Value>::put(key, value);
            return;
        }
        touchAndMaybePromote(key, value);
    }

    /// @brief 获取缓存值
    /// @param key 键
    /// @param value 值
    /// @return 从主缓存中取到值则返回True,同时修改传参value;不在缓存中则返回false.
    bool get(const Key& key, Value& value) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if (LruCache<Key,Value>::get(key, value)) {
            return true;
        }
        // 仅用原值进行 promote
        auto it = historyStore_.find(key);
        if (it != historyStore_.end()) {
            touchAndMaybePromote(key, it->second);
            // promote 后保证在主缓存中
            return LruCache<Key,Value>::get(key, value);
        }
        // 第一次 miss，无任何值；也要更新计数，但不存 value
        touchCountOnly(key);
        return false;
    }

    /// @brief 移出特定缓存
    /// @param key 
    void remove(const Key& key) override {
        std::lock_guard<std::mutex> lk(mtx_);
        LruCache<Key,Value>::remove(key);
        history_.remove(key);
        historyStore_.erase(key);
    }

    /// @brief 清空缓存
    void removeAll() override {
        std::lock_guard<std::mutex> lk(mtx_);
        LruCache<Key,Value>::removeAll();
        history_.removeAll();
        historyStore_.clear();
    }
    

private:
    void touchAndMaybePromote(const Key& key, const Value& val) {
        size_t cnt = 0;
        history_.get(key, cnt);
        ++cnt;
        if (cnt >= k_) {
            LruCache<Key,Value>::put(key, val);
            history_.remove(key);
            historyStore_.erase(key);
        } else {
            history_.put(key, cnt);
            historyStore_[key] = val;
        }
    }

    void touchCountOnly(const Key& key) {
        size_t cnt = 0;
        history_.get(key, cnt);
        history_.put(key, cnt + 1);
    }

private:
    int k_;//数据存入主缓存访问次数设定
    LruCache<Key, size_t> history_;//历史记录
    std::unordered_map<Key, Value> historyStore_;//历史数据存储
    std::mutex mtx_;
};

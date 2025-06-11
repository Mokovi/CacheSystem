#pragma once

#include "CachePolicy.hpp"
#include "LruCache.hpp"
#include "LfuCache.hpp"

#include <list>
#include <unordered_set>
#include <mutex>
#include <cassert>

/**
 * ARC (Adaptive Replacement Cache) 实现，结合 LRU (T1) 和 LFU (T2)
 * 继承自 CachePolicy<Key, Value>
 */
template<typename Key, typename Value>
class ArcCache : public CachePolicy<Key, Value> {
public:
    /**
     * 构造函数：
     * @param capacity 最大缓存容量
     * p_ 初始为 0，用于动态平衡 T1 与 T2 大小
     */
    explicit ArcCache(int capacity)
        : capacity_(capacity), p_(0), t1_(capacity), t2_(capacity) {
        assert(capacity_ > 0 && "capacity must be > 0");
    }

    /**
     * 插入或更新缓存项：
     * - 若命中 T1：移至 T2
     * - 若命中 T2：更新值
     * - 若在幽灵列表 B1/B2：根据命中列表调整 p_，并调用 replace
     * - 未命中：根据当前大小和容量，执行驱逐逻辑，再插入至 T1
     */
    void put(const Key& key, const Value& value) override {
        std::lock_guard<std::mutex> lg(mtx_);

        // 命中 T1，提升到 T2
        if (t1_.get(key, tmp_)) {
            t1_.remove(key);
            t2_.put(key, value);
            return;
        }
        // 命中 T2，直接更新
        if (t2_.get(key, tmp_)) {
            t2_.put(key, value);
            return;
        }

        // 命中幽灵列表 B1：增大 p_，移除旧 B1，执行替换，再插入 T2
        if (b1_set_.count(key)) {
            p_ = std::min(capacity_, p_ + std::max((int)b2_.size() / (int)b1_.size(), 1));
            replace(key);
            b1_remove(key);
            t2_.put(key, value);
            return;
        }
        // 命中幽灵列表 B2：减小 p_，同上逻辑
        if (b2_set_.count(key)) {
            p_ = std::max(0, p_ - std::max((int)b1_.size() / (int)b2_.size(), 1));
            replace(key);
            b2_remove(key);
            t2_.put(key, value);
            return;
        }

        // 缓存未命中：处理容量 - 若 T1+B1 达到容量
        if ((int)t1_size() + (int)b1_.size() == capacity_) {
            if (t1_size() < capacity_) {
                // 删除 B2 最旧，再替换
                b2_evict();
                replace(key);
            } else {
                // T1 满，驱逐 T1 最旧到 B1
                auto victim = t1_evict();
                b1_add(victim);
            }
        }
        // 若总大小超过 2*capacity，则驱逐 B2 最旧或执行替换
        else if ((int)t1_size() + (int)t2_size() + (int)b1_.size() + (int)b2_.size() >= capacity_) {
            if ((int)t1_size() + (int)t2_size() + (int)b1_.size() + (int)b2_.size() == 2 * capacity_) {
                b2_evict();
            }
            replace(key);
        }
        // 最终插入 T1
        t1_.put(key, value);
    }

    /**
     * 获取缓存项：
     * - 若命中 T1：提升到 T2 并返回 true
     * - 若命中 T2：直接返回 true
     * - 否则返回 false
     */
    bool get(const Key& key, Value& value) override {
        std::lock_guard<std::mutex> lg(mtx_);
        if (t1_.get(key, value)) {
            t1_.remove(key);
            t2_.put(key, value);
            return true;
        }
        if (t2_.get(key, value)) {
            return true;
        }
        return false;
    }

    /**
     * 简化版 get：不存在则返回 Value{}
     */
    Value get(const Key& key) override {
        Value tmp{};
        return get(key, tmp) ? tmp : Value{};
    }

    /**
     * 删除指定键：从所有列表中移除
     */
    void remove(const Key& key) override {
        std::lock_guard<std::mutex> lg(mtx_);
        t1_.remove(key);
        t2_.remove(key);
        b1_remove(key);
        b2_remove(key);
    }

    /**
     * 清空缓存：重置所有子缓存与幽灵列表，p_ 置 0
     */
    void removeAll() override {
        std::lock_guard<std::mutex> lg(mtx_);
        t1_.removeAll();
        t2_.removeAll();
        b1_.clear(); b1_set_.clear();
        b2_.clear(); b2_set_.clear();
        p_ = 0;
    }

private:
    /**
     * 替换策略：
     * - 若 T1 大小超过 p_，或在 B2 且 T1 大小等于 p_：从 T1 驱逐并加入 B1
     * - 否则从 T2 驱逐并加入 B2
     */
    void replace(const Key& key) {
        if (t1_size() > 0 && (t1_size() > p_ || (b2_set_.count(key) && t1_size() == p_))) {
            auto victim = t1_evict();
            b1_add(victim);
        } else {
            auto victim = t2_evict();
            b2_add(victim);
        }
    }

    // 子缓存及幽灵列表相关辅助方法
    size_t t1_size() const { return t1_.size(); }
    size_t t2_size() const { return t2_.size(); }
    Key t1_evict() { return t1_.evictOldest(); }
    Key t2_evict() { return t2_.evictLeastFrequent(); }

    void b1_add(const Key& key) { b1_.push_front(key); b1_set_.insert(key); }
    void b1_remove(const Key& key) { if (b1_set_.erase(key)) b1_.remove(key); }
    void b1_evict() { if (!b1_.empty()) { b1_set_.erase(b1_.back()); b1_.pop_back(); } }

    void b2_add(const Key& key) { b2_.push_front(key); b2_set_.insert(key); }
    void b2_remove(const Key& key) { if (b2_set_.erase(key)) b2_.remove(key); }
    void b2_evict() { if (!b2_.empty()) { b2_set_.erase(b2_.back()); b2_.pop_back(); } }

private:
    int capacity_;               // 缓存容量
    int p_;                      // 控制 T1 与 T2 大小分配的参数

    LruCache<Key, Value> t1_;    // 最近访问(RECENT)缓存
    LfuCache<Key, Value> t2_;    // 高频访问(FREQUENT)缓存

    std::list<Key> b1_, b2_;     // 幽灵列表：只存键，用于历史反馈
    std::unordered_set<Key> b1_set_, b2_set_;

    std::mutex mtx_;             // 线程安全
    Value tmp_;                  // 临时存储查询值
};

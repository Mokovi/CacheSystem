#pragma once

#include <unordered_map>
#include <list>
#include <cstddef>
#include <stdexcept>
#include <mutex>
#include "CachePolicy.hpp"

/**
 * ARCCache: Adaptive Replacement Cache 实现
 * 核心结构: 四条链表 T1, T2, B1, B2
 * - T1: 最近新插入且访问一次的数据 (LRU)
 * - T2: 访问多次的热点数据 (LFU 效果)
 * - B1: T1 淘汰出的幽灵记录
 * - B2: T2 淘汰出的幽灵记录
 * 自适应参数 p 控制 T1 与 T2 大小分配
 */
template<typename Key, typename Value>
class ARCCache : public CachePolicy<Key, Value> {
public:
    explicit ARCCache(std::size_t capacity)
        : capacity_(capacity), p_(0) {
        if (capacity_ == 0) throw std::invalid_argument("Capacity must be > 0");
    }

    // 插入或更新数据
    void put(const Key& key, const Value& value) override {
        std::lock_guard<std::mutex> lock(mu_);
        // 命中 T1 或 T2: 更新并 promote
        if (promoteIfExists(key, value)) return;

        // 幽灵命中: 调整 p 并处理
        if (handleGhostHit(key, value, b1_map_, b1_list_, /*isB1=*/true)) return;
        if (handleGhostHit(key, value, b2_map_, b2_list_, /*isB1=*/false)) return;

        // 全新写入路径
        handleCacheMiss(key, value);
    }

    // 获取数据 (带输出参数)
    bool get(const Key& key, Value& value) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (accessT1(key) || accessT2(key)) {
            value = store_map_[key];
            return true;
        }
        return false;
    }

    // 获取数据 (抛异常版本)
    Value get(const Key& key) override {
        Value v;
        if (!get(key, v)) throw std::out_of_range("Key not found");
        return v;
    }

    // 删除指定 key
    void remove(const Key& key) override {
        std::lock_guard<std::mutex> lock(mu_);
        eraseKey(key, t1_list_, t1_map_);
        eraseKey(key, t2_list_, t2_map_);
        eraseKey(key, b1_list_, b1_map_);
        eraseKey(key, b2_list_, b2_map_);
    }

    // 清空所有缓存与记录
    void removeAll() override {
        std::lock_guard<std::mutex> lock(mu_);
        t1_list_.clear(); t1_map_.clear();
        t2_list_.clear(); t2_map_.clear();
        b1_list_.clear(); b1_map_.clear();
        b2_list_.clear(); b2_map_.clear();
        store_map_.clear();
        p_ = 0;
    }

private:
    // 如果 key 在 T1/T2 中则 promote 并更新 value
    bool promoteIfExists(const Key& key, const Value& value) {
        auto it_t1 = t1_map_.find(key);
        if (it_t1 != t1_map_.end()) {
            moveKey(t1_list_, t1_map_, t2_list_, t2_map_, key);
            store_map_[key] = value;
            return true;
        }
        auto it_t2 = t2_map_.find(key);
        if (it_t2 != t2_map_.end()) {
            // 刷新 MRU
            t2_list_.erase(it_t2->second);
            t2_list_.push_back(key);
            t2_map_[key] = std::prev(t2_list_.end());
            store_map_[key] = value;
            return true;
        }
        return false;
    }

    // 访问 T1/T2，仅做 promote，不更新 value
    bool accessT1(const Key& key) {
        auto it = t1_map_.find(key);
        if (it == t1_map_.end()) return false;
        moveKey(t1_list_, t1_map_, t2_list_, t2_map_, key);
        return true;
    }
    bool accessT2(const Key& key) {
        auto it = t2_map_.find(key);
        if (it == t2_map_.end()) return false;
        // 刷新 MRU
        t2_list_.erase(it->second);
        t2_list_.push_back(key);
        t2_map_[key] = std::prev(t2_list_.end());
        return true;
    }

    // 处理幽灵命中，返回 true 表示已处理完成
    template<typename GhostMap, typename GhostList>
    bool handleGhostHit(const Key& key, const Value& value,
                        GhostMap& ghost_map, GhostList& ghost_list, bool isB1) {
        auto it = ghost_map.find(key);
        if (it == ghost_map.end()) return false;
        // 调整 p
        adjustP(isB1);
        // 淘汰一个槽位
        evictSlot(key);
        // 从幽灵提升到 T2
        ghost_list.erase(it->second);
        ghost_map.erase(key);
        insertToT2(key, value);
        return true;
    }

    // 处理全新 Miss 路径
    void handleCacheMiss(const Key& key, const Value& value) {
        if (t1_list_.size() + t2_list_.size() >= capacity_) {
            evictSlot(key);
        }
        // 保证 B1+B2 ≤ capacity_
        trimGhostLists();
        // 插入到 T1
        insertToT1(key, value);
    }

    // 调整比例 p
    void adjustP(bool hitB1) {
        if (hitB1) {
            p_ = std::min(capacity_, p_ + std::max(b2_list_.size() / b1_list_.size(), size_t(1)));
        } else {
            p_ = (p_ > 0
                  ? p_ - std::max(b1_list_.size() / b2_list_.size(), size_t(1))
                  : 0);
        }
    }

    // 淘汰一个槽位：从 T1 或 T2
    void evictSlot(const Key& incoming) {
        if (!t1_list_.empty() && (t1_list_.size() > p_ ||
            (b2_map_.count(incoming) && t1_list_.size() == p_))) {
            evictFromT1();
        } else {
            evictFromT2();
        }
    }
    void evictFromT1() {
        Key old = t1_list_.front();
        t1_list_.pop_front();
        t1_map_.erase(old);
        b1_list_.push_back(old);
        b1_map_[old] = std::prev(b1_list_.end());
    }
    void evictFromT2() {
        Key old = t2_list_.front();
        t2_list_.pop_front();
        t2_map_.erase(old);
        b2_list_.push_back(old);
        b2_map_[old] = std::prev(b2_list_.end());
    }

    // 限制 ghost 大小
    void trimGhostLists() {
        while (b1_list_.size() + b2_list_.size() > capacity_) {
            if (b1_list_.size() > capacity_ - p_) {
                removeGhost(b1_list_, b1_map_);
            } else {
                removeGhost(b2_list_, b2_map_);
            }
        }
    }
    void removeGhost(std::list<Key>& list, std::unordered_map<Key, typename std::list<Key>::iterator>& map) {
        Key old = list.front();
        list.pop_front();
        map.erase(old);
    }

    // 插入到 T1 或 T2
    void insertToT1(const Key& key, const Value& value) {
        store_map_[key] = value;
        t1_list_.push_back(key);
        t1_map_[key] = std::prev(t1_list_.end());
    }
    void insertToT2(const Key& key, const Value& value) {
        store_map_[key] = value;
        t2_list_.push_back(key);
        t2_map_[key] = std::prev(t2_list_.end());
    }

    // 工具: 从一个链表+map 中删除 key
    template<typename List, typename Map>
    void eraseKey(const Key& key, List& list, Map& map) {
        auto it = map.find(key);
        if (it != map.end()) {
            list.erase(it->second);
            map.erase(it);
            store_map_.erase(key);
        }
    }

    // 工具: 从 srcList/srcMap 移动 key 到 dstList/dstMap
    template<typename List, typename Map>
    void moveKey(List& srcList, Map& srcMap,
                 List& dstList, Map& dstMap,
                 const Key& key) {
        auto it = srcMap[key];
        srcList.erase(it);
        srcMap.erase(key);
        dstList.push_back(key);
        dstMap[key] = std::prev(dstList.end());
    }

private:
    const std::size_t capacity_;
    std::size_t p_;  // 自适应比例参数
    std::mutex mu_;  // 线程安全锁

    // 主存储 map (key -> value)
    std::unordered_map<Key, Value> store_map_;

    // T1 和 T2 链表及映射
    std::list<Key> t1_list_, t2_list_;
    std::unordered_map<Key, typename std::list<Key>::iterator> t1_map_, t2_map_;

    // 幽灵链表 B1, B2 及映射 (仅保存 key)
    std::list<Key> b1_list_, b2_list_;
    std::unordered_map<Key, typename std::list<Key>::iterator> b1_map_, b2_map_;
};

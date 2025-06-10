#pragma once

#include "CachePolicy.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>

template<typename Key, typename Value>
class LfuCache : public CachePolicy<Key, Value> {
public:
    explicit LfuCache(int capacity)
        : capacity_(capacity), minFreq_(0)
    {
        assert(capacity_ > 0 && "capacity must be > 0");
    }

    void put(const Key& key, const Value& value) override {
        std::lock_guard<std::mutex> lg(mtx_);
        if (capacity_ <= 0) return;

        auto it = nodes_.find(key);
        if (it != nodes_.end()) {
            // 已存在：更新值并提升频率
            auto node = it->second;
            node->value = value;
            touch(node);
        } else {
            // 新节点：容量满则淘汰
            if ((int)nodes_.size() >= capacity_) {
                auto& dl = freqList_[minFreq_];
                auto victim = dl.head->next;
                removeNode(victim);
                nodes_.erase(victim->key);
            }
            // 插入新节点，freq=1
            auto newNode = std::make_shared<Node>(key, value, /*freq=*/1);
            nodes_[key] = newNode;
            minFreq_ = 1;
            freqList_[1].addToTail(newNode);
        }
    }

    bool get(const Key& key, Value& value) override {
        std::lock_guard<std::mutex> lg(mtx_);
        auto it = nodes_.find(key);
        if (it == nodes_.end()) return false;

        auto node = it->second;
        value = node->value;
        touch(node);
        return true;
    }

    Value get(const Key& key) override {
        Value tmp{};
        return get(key, tmp) ? tmp : Value{};
    }

    void remove(const Key& key) override {
        std::lock_guard<std::mutex> lg(mtx_);
        auto it = nodes_.find(key);
        if (it == nodes_.end()) return;

        removeNode(it->second);
        nodes_.erase(it);
        findMinFreq();
    }

    void removeAll() override {
        std::lock_guard<std::mutex> lg(mtx_);
        nodes_.clear();
        freqList_.clear();
        minFreq_ = 0;
    }

private:
    struct Node {
        Key      key;
        Value    value;
        int      freq;
        std::weak_ptr<Node> prev;
        std::shared_ptr<Node> next;

        // 默认构造给哑结点或占位用
        Node(const Key& k = Key{}, const Value& v = Value{}, int f = 0)
            : key(k), value(v), freq(f) {}
    };

    // 双向链表：维持同频率节点的访问顺序
    struct DoubleList {
        std::shared_ptr<Node> head;
        std::shared_ptr<Node> tail;

        DoubleList() {
            head = std::make_shared<Node>(Key{}, Value{}, /*freq=*/0);
            tail = std::make_shared<Node>(Key{}, Value{}, /*freq=*/0);
            head->next = tail;
            tail->prev = head;
        }

        void addToTail(const std::shared_ptr<Node>& node) {
            auto prev = tail->prev.lock();
            prev->next = node;
            node->prev = prev;
            node->next = tail;
            tail->prev = node;
        }

        void removeNode(const std::shared_ptr<Node>& node) {
            assert(node->freq > 0 && "attempt to remove invalid node");
            auto prev = node->prev.lock();
            if (prev) {
                prev->next = node->next;
                node->next->prev = prev;
            }
        }

        bool empty() const {
            return head->next == tail;
        }
    };

    // 访问一次 node：从旧 freq 链表移除，freq+1 后加入新链表
    void touch(const std::shared_ptr<Node>& node) {
        int f = node->freq;
        auto& oldList = freqList_[f];
        oldList.removeNode(node);

        if (f == minFreq_ && oldList.empty()) {
            freqList_.erase(f);
            minFreq_++;//直接加一
        }

        node->freq = f + 1;
        freqList_[node->freq].addToTail(node);
    }

    // 真正移除 node（用于淘汰或 remove(key)）
    void removeNode(const std::shared_ptr<Node>& node) {
        int f = node->freq;
        auto& dl = freqList_[f];
        dl.removeNode(node);

        if (f == minFreq_ && dl.empty()) {
            freqList_.erase(f);
            // 注意：此处不 ++minFreq_，直接重扫描
            findMinFreq();
        }
    }

    // 重新扫描所有 freqList_，找到最小非空 freq
    void findMinFreq() {
        int best = std::numeric_limits<int>::max();
        for (auto& p : freqList_) {
            if (!p.second.empty() && p.first < best) {
                best = p.first;
            }
        }
        minFreq_ = (best == std::numeric_limits<int>::max()) ? 0 : best;
    }

    int capacity_;
    int minFreq_;
    std::unordered_map<Key, std::shared_ptr<Node>> nodes_;
    std::unordered_map<int, DoubleList> freqList_;
    std::mutex mtx_;
};

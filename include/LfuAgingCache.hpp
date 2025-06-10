#pragma once
#include "CachePolicy.hpp"
#include <iostream>
#include <memory>
#include <unordered_map>
#include <cmath>

template<class Key, class Value>
class LfuAgingCache : public CachePolicy<Key, Value> {
public:
    explicit LfuAgingCache(int capacity, double maxAvgFreqLimit = 10.0)
        : capacity_(capacity)
        , minFreq_(0)
        , maxAvgFreqLimit_(maxAvgFreqLimit)
        , totalFreqSum_(0)
    {}

    void put(const Key& key, const Value& value) override {
        if (capacity_ <= 0) return;

        auto it = nodes_.find(key);
        if (it != nodes_.end()) {
            // 已有节点：更新值并 touch
            auto node = it->second;
            node->value = value;
            touch(node);
        } else {
            // 容量已满，淘汰 minFreq_ 链表头部
            if ((int)nodes_.size() >= capacity_) {
                auto& list = freqList_[minFreq_];
                auto toRemove = list.head->next;
                removeNode(toRemove);
                nodes_.erase(toRemove->key);
            }
            // 插入新节点，freq 初始为 1
            auto newNode = std::make_shared<Node>(key, value);
            nodes_[key] = newNode;
            minFreq_ = 1;
            freqList_[1].addToTail(newNode);
            totalFreqSum_ += 1;
            tryAging();
        }
    }

    bool get(const Key& key, Value& value) override {
        auto it = nodes_.find(key);
        if (it == nodes_.end()) return false;
        auto node = it->second;
        value = node->value;
        touch(node);
        return true;
    }

    Value get(const Key& key) override {
        Value v;
        return get(key, v) ? v : Value{};
    }

    void remove(const Key& key) override {
        auto it = nodes_.find(key);
        if (it == nodes_.end()) return;
        auto node = it->second;
        totalFreqSum_ -= node->freq;
        removeNode(node);
        nodes_.erase(it);
    }

    void removeAll() override {
        nodes_.clear();
        freqList_.clear();
        minFreq_ = 0;
        totalFreqSum_ = 0;
    }

private:
    struct Node {
        Key key;
        Value value;
        int freq;
        std::weak_ptr<Node> prev;
        std::shared_ptr<Node> next;
        Node(const Key& k, const Value& v)
            : key(k), value(v), freq(1) {}
    };

    struct DoubleList {
        std::shared_ptr<Node> head;
        std::shared_ptr<Node> tail;
        DoubleList() {
            head = std::make_shared<Node>(Key{}, Value{});
            tail = std::make_shared<Node>(Key{}, Value{});
            head->next = tail;
            tail->prev = head;
        }
        void addToTail(const std::shared_ptr<Node>& node) {
            auto last = tail->prev.lock();
            last->next = node;
            node->prev = last;
            node->next = tail;
            tail->prev = node;
        }
        void removeNode(const std::shared_ptr<Node>& node) {
            auto p = node->prev.lock();
            if (p) {
                p->next = node->next;
                node->next->prev = p;
            }
        }
        bool empty() const {
            return head->next == tail;
        }
    };

    void touch(const std::shared_ptr<Node>& node) {
        // 从旧 freq 链表中移除
        int oldFreq = node->freq;
        freqList_[oldFreq].removeNode(node);
        if (oldFreq == minFreq_ && freqList_[oldFreq].empty()) {
            minFreq_++;
            freqList_.erase(oldFreq);
        }
        // 增加 freq
        node->freq++;
        totalFreqSum_++;  // 访问一次，加 1 次计数
        // 插入到新 freq 链表末尾
        freqList_[node->freq].addToTail(node);

        tryAging();
    }

    void removeNode(const std::shared_ptr<Node>& node) {
        auto freq = node->freq;
        freqList_[freq].removeNode(node);
        if (freq == minFreq_ && freqList_[freq].empty()) {
            ++minFreq_;
            freqList_.erase(freq);
        }
    }

    void tryAging() {
        if (nodes_.empty()) return;
        double avgFreq = double(totalFreqSum_) / nodes_.size();
        if (avgFreq > maxAvgFreqLimit_) {
            ageAll();
        }
    }

    void ageAll() {
        // 每个节点都减去 (maxAvgFreqLimit_ / 2)，最低保留为 1
        int delta = static_cast<int>(std::floor(maxAvgFreqLimit_ / 2.0));
        totalFreqSum_ = 0;
        // 先清空所有 freqList_
        freqList_.clear();
        minFreq_ = 0;

        for (auto& [k, node] : nodes_) {
            node->freq = std::max(1, node->freq - delta);
            totalFreqSum_ += node->freq;
            // 重建双向链表
            freqList_[node->freq].addToTail(node);
            minFreq_ = std::min(minFreq_, node->freq);
        }
    }



    int capacity_;
    int minFreq_;
    double maxAvgFreqLimit_;
    size_t totalFreqSum_;
    std::unordered_map<Key, std::shared_ptr<Node>> nodes_;
    std::unordered_map<int, DoubleList> freqList_;
};

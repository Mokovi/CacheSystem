#pragma once

#include "CachePolicy.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <cassert>
#include <cmath>
#include <limits>

template<class Key, class Value>
class LfuAgingCache : public CachePolicy<Key, Value> {
public:
    explicit LfuAgingCache(int capacity, double maxAvgFreqLimit = 10.0)
        : capacity_(capacity)
        , minFreq_(0)
        , maxAvgFreqLimit_(maxAvgFreqLimit)
        , totalFreqSum_(0)
    {
        assert(capacity_ > 0 && "capacity must be > 0");
    }

    void put(const Key& key, const Value& value) override {
        if (capacity_ <= 0) return;
        std::lock_guard<std::mutex> lock(mtx_);

        auto it = nodes_.find(key);
        if (it != nodes_.end()) {
            // 更新已有节点
            it->second->value = value;
            touch(it->second);
        } else {
            // 淘汰
            evictIfNeeded();
            // 插入新节点
            auto node = std::make_shared<Node>(key, value, 1);
            nodes_[key] = node;
            totalFreqSum_ += 1;
            freqList_[1].addToTail(node);
            minFreq_ = 1;
            tryAging();
        }
    }

    bool get(const Key& key, Value& value) override {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = nodes_.find(key);
        if (it == nodes_.end()) return false;
        auto node = it->second;
        value = node->value;
        touch(node);
        return true;
    }


    Value get(const Key& key) override {
        Value tmp;
        return get(key, tmp) ? tmp : Value{};
    }

    void remove(const Key& key) override {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = nodes_.find(key);
        if (it == nodes_.end()) return;
        auto node = it->second;
        totalFreqSum_ -= node->freq;
        freqList_[node->freq].removeNode(node);
        nodes_.erase(it);
        rebuildMinFreq();
    }

    void removeAll() override {
        std::lock_guard<std::mutex> lock(mtx_);
        nodes_.clear();
        freqList_.clear();
        minFreq_ = 0;
        totalFreqSum_ = 0;
    }

private:
    struct Node {
        Key      key;
        Value    value;
        int      freq;
        std::weak_ptr<Node> prev;
        std::shared_ptr<Node> next;

        // 普通节点构造：freq 初始为 1
        Node(const Key& k, const Value& v, int f)
            : key(k), value(v), freq(f) {}

        // 哑结点专用构造：可传 freq = 0
        Node() : freq(0) {}
    };

    struct DoubleList {
        std::shared_ptr<Node> head;
        std::shared_ptr<Node> tail;

        DoubleList() {
            head = std::make_shared<Node>(); // freq = 0
            tail = std::make_shared<Node>(); // freq = 0
            head->next = tail;
            tail->prev = head;
        }

        // 将 node 插到尾部（在 tail 之前）
        void addToTail(const std::shared_ptr<Node>& node) {
            auto prev = tail->prev.lock();
            assert(prev && "prev must exist");
            prev->next = node;
            node->prev = prev;
            node->next = tail;
            tail->prev = node;
        }

        void removeNode(const std::shared_ptr<Node>& node) {
            auto prev = node->prev.lock();
            assert(prev && "node must be in list");
            prev->next = node->next;
            node->next->prev = prev;
        }

        bool empty() const {
            return head->next == tail;
        }
    };

    // 访问/更新时调用
    void touch(const std::shared_ptr<Node>& node) {
        int oldF = node->freq;
        // 从旧桶中移除
        freqList_[oldF].removeNode(node);
        totalFreqSum_ -= oldF;

        // 更新频率
        node->freq++;
        totalFreqSum_ += node->freq;

        // 放入新桶
        freqList_[node->freq].addToTail(node);

        // 如果旧桶空，则清理并重建 minFreq_
        if (freqList_[oldF].empty()) {
            freqList_.erase(oldF);
            if (oldF == minFreq_) 
                //rebuildMinFreq();
                minFreq_++;//从touch调用的一定会是下一个频率
        }

        tryAging();
    }

    // 如果超出容量，就淘汰当前 minFreq_ 桶的头节点
    void evictIfNeeded() {
        if ((int)nodes_.size() < capacity_) return;
        auto& lst = freqList_[minFreq_];
        auto victim = lst.head->next;
        // 更新总频率
        totalFreqSum_ -= victim->freq;
        // 从 list/map 中移除
        lst.removeNode(victim);
        if (lst.empty()) freqList_.erase(minFreq_);
        nodes_.erase(victim->key);
        // 重建新的 minFreq_
        rebuildMinFreq();
    }

    // 重新扫描所有频率桶，找到最小 freq
    void rebuildMinFreq() {
        minFreq_ = std::numeric_limits<int>::max();
        for (auto& [f, lst] : freqList_) {
            if (!lst.empty() && f < minFreq_) {
                minFreq_ = f;
            }
        }
        if (minFreq_ == std::numeric_limits<int>::max()) {
            minFreq_ = 0;
        }
    }

    // 判断是否需要触发老化
    void tryAging() {
        if (nodes_.empty()) return;
        double avg = double(totalFreqSum_) / nodes_.size();
        if (avg > maxAvgFreqLimit_) {
            ageAll();
        }
    }

    // 对所有节点做“老化”：freq -= floor(maxAvgFreqLimit_/2)，下界为 1
    void ageAll() {
        int delta = static_cast<int>(std::floor(maxAvgFreqLimit_ / 2.0));
        totalFreqSum_ = 0;
        // 清空所有 freqList_
        freqList_.clear();
        minFreq_ = std::numeric_limits<int>::max();

        for (auto& [k, node] : nodes_) {
            node->freq = std::max(1, node->freq - delta);
            totalFreqSum_ += node->freq;
            freqList_[node->freq].addToTail(node);
            if (node->freq < minFreq_) {
                minFreq_ = node->freq;
            }
        }
        if (minFreq_ == std::numeric_limits<int>::max()) {
            minFreq_ = 0;
        }
    }

private:
    int capacity_;
    int minFreq_;
    double maxAvgFreqLimit_;
    size_t totalFreqSum_;

    std::unordered_map<Key, std::shared_ptr<Node>> nodes_;
    std::unordered_map<int, DoubleList>           freqList_;
    std::mutex                                    mtx_;
};

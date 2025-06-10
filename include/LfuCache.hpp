#pragma once
#include "CachePolicy.hpp"
#include <iostream>
#include <memory>
#include <unordered_map>

template<class Key, class Value>
class LfuCache : public CachePolicy<Key, Value> {
public:
    /// @brief 构造函数
    /// @param capacity 缓存容量
    explicit LfuCache(int capacity)
        : capacity_(capacity), minFreq_(0) {
    }


    /// @brief 添加/更新缓存
    /// @param key  键
    /// @param value 值
    void put(const Key& key, const Value& value) override {
        if (capacity_ <= 0) return;

        auto it = nodes_.find(key);
        if (it != nodes_.end()) {
            auto node = it->second;
            node->value = value;
            touch(node);
        } else {
            if ((int)nodes_.size() >= capacity_) {
                auto& list = freqList_[minFreq_];
                auto toRemove = list.head->next;
                removeNode(toRemove);
                nodes_.erase(toRemove->key);
            }
            auto newNode = std::make_shared<Node>(key, value);
            nodes_[key] = newNode;
            minFreq_ = 1;
            freqList_[1].addToTail(newNode);
        }
    }

    /// @brief 获取缓存数据
    /// @param key 键
    /// @param value 值
    /// @return True：键在缓存中，值被写入输入参数value; False: 未存储
    bool get(const Key& key, Value& value) override {
        auto it = nodes_.find(key);
        if (it == nodes_.end()) return false;
        auto node = it->second;
        value = node->value;
        touch(node);
        return true;
    }
    
    /// @brief 获取缓存数据
    /// @param key 
    /// @return 存在则返回，key所对应的值。不存在则返回value默认无参构造
    Value get(const Key& key) override {
        Value value;
        return get(key, value) ? value : Value{};
    }


    /// @brief 删除对应键的缓存
    /// @param key 
    void remove(const Key& key) override {
        auto it = nodes_.find(key);
        if (it == nodes_.end()) return;
        auto node = it->second;
        removeNode(node);
        nodes_.erase(it);
    }

    /// @brief 清空缓存
    void removeAll() override {
        nodes_.clear();
        freqList_.clear();
        minFreq_ = 0;
    }

private:
    struct Node {
        Key key;
        Value value;
        int freq;
        std::weak_ptr<Node> prev; //使用weak_ptr 避免循环引用
        std::shared_ptr<Node> next;
        Node(const Key& k, const Value& v)
            : key(k), value(v), freq(1){} //智能指针内置了初始化，不需要重复初始化
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
            auto latest = tail->prev.lock();
            latest->next = node;
            node->prev = latest;
            node->next = tail;
            tail->prev = node;
        }
        void removeNode(const std::shared_ptr<Node>& node) {
            auto prevShared = node->prev.lock();
            if (prevShared) {
                prevShared->next = node->next;
                node->next->prev = prevShared;
            }
        }
        bool empty() const {
            return head->next == tail;
        }
    };

    void touch(const std::shared_ptr<Node>& node) {
        int freq = node->freq;
        freqList_[freq].removeNode(node);
        if (freq == minFreq_ && freqList_[freq].empty()) {
            ++minFreq_;
        }
        ++node->freq;
        freqList_[node->freq].addToTail(node);
    }

    void removeNode(const std::shared_ptr<Node>& node) {
        auto freq = node->freq;
        freqList_[freq].removeNode(node);
        if (freq == minFreq_ && freqList_[freq].empty()) {
            ++minFreq_;
            freqList_.erase(freq);
        }
    }

    int capacity_;
    int minFreq_;
    std::unordered_map<Key, std::shared_ptr<Node>> nodes_;
    std::unordered_map<int, DoubleList> freqList_;
};

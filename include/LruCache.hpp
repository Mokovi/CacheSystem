#pragma once

#include <memory>
#include <unordered_map>
#include <mutex>
#include <cassert>
#include "cachePolicy.hpp"


// 前向声明
template<typename Key, typename Value>
class LruNode;
template<typename Key, typename Value>
class LruCache;

/// LRU 缓存使用的节点结构（双向链表）
template<typename Key, typename Value>
class LruNode {
    friend class LruCache<Key, Value>;
public:
    LruNode(const Key& k, const Value& v) : key(k), value(v), access_count(1) {}
    const Key& getKey()   const { return key; }
    const Value& getValue() const { return value; }
    void setValue(const Value& v) { value = v; }
    size_t getAccessCount() const { return access_count; }
    void incrementAccessCount() { ++access_count; }

private:
    Key key;
    Value value;
    size_t access_count;
    std::weak_ptr<LruNode> prev;
    std::shared_ptr<LruNode> next;
};

/// LRU 缓存实现（线程安全、支持泛型）
/// 存储结构： 双向链表 + 哈希表 
template<typename Key, typename Value>
class LruCache : public CachePolicy<Key, Value> {
public:
    using Node    = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit LruCache(int capacity) : m_capacity(capacity) {
        assert(capacity >= 0 && "Capacity must be non-negative");
        initializeList();
    }

    /// @brief 添加/更新缓存
    /// @param key  键
    /// @param value 值
    void put(const Key& key, const Value& value) override {
        if (m_capacity <= 0) return;
        std::lock_guard<std::mutex> guard(m_mutex);

        auto it = m_node_map.find(key);
        if (it != m_node_map.end()) {
            updateExistingNode(it->second, value);
        } else {
            if (static_cast<int>(m_node_map.size()) >= m_capacity) {
                evictLeastRecent();
            }
            addNewNode(key, value);
        }
    }

    /// @brief 获取缓存数据
    /// @param key 键
    /// @param value 值
    /// @return True：键在缓存中，值被写入输入参数value; False: 未存储
    bool get(const Key& key, Value& value) override {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_node_map.find(key);
        if (it == m_node_map.end()) return false;

        auto node = it->second;
        value = node->getValue();
        moveToMostRecent(node);
        return true;
    }

    /// @brief 获取缓存数据
    /// @param key 
    /// @return 存在则返回，key所对应的值。不存在则返回value默认无参构造
    Value get(const Key& key) override {
        Value temp{};
        get(key, temp);
        return temp;
    }

    /// @brief 删除对应键的缓存
    /// @param key 
    void remove(const Key& key) override {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_node_map.find(key);
        if (it == m_node_map.end()) return;

        removeNode(it->second);
        m_node_map.erase(it);
    }
    /// @brief 清空缓存
    void removeAll() override {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_node_map.clear();
        initializeList();
    }

private:
    void initializeList() {
        m_head = std::make_shared<Node>(Key(), Value());
        m_tail = std::make_shared<Node>(Key(), Value());
        m_head->next = m_tail;
        m_tail->prev = m_head;
    }

    void updateExistingNode(NodePtr node, const Value& value) {
        node->setValue(value);
        moveToMostRecent(node);
    }

    void addNewNode(const Key& key, const Value& value) {
        NodePtr newNode = std::make_shared<Node>(key, value);
        insertNodeAtTail(newNode);
        m_node_map.emplace(key, newNode);
    }

    void moveToMostRecent(NodePtr node) {
        removeNode(node);
        insertNodeAtTail(node);
        node->incrementAccessCount();
    }

    void insertNodeAtTail(NodePtr node) {
        auto prevReal = m_tail->prev.lock();
        if (!prevReal) return;  // just in case
        prevReal->next = node;
        node->prev     = prevReal;
        node->next     = m_tail;
        m_tail->prev   = node;
    }

    void removeNode(NodePtr node) {
        auto prevNode = node->prev.lock();
        auto nextNode = node->next;
        if (prevNode && nextNode) {
            prevNode->next = nextNode;
            nextNode->prev = prevNode;
        }
        node->prev.reset();
        node->next.reset();
    }

    void evictLeastRecent() {
        auto oldest = m_head->next;
        if (oldest == m_tail) return;
        removeNode(oldest);
        m_node_map.erase(oldest->getKey());
    }

private:
    int       m_capacity;
    NodeMap   m_node_map;
    std::mutex m_mutex;
    NodePtr   m_head;
    NodePtr   m_tail;
};

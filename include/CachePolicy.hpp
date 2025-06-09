#pragma once

/// 缓存策略接口
template<typename Key, typename Value>
class CachePolicy {
public:
    virtual ~CachePolicy() = default;
    virtual void put(const Key& key, const Value& value) = 0;
    virtual bool get(const Key& key, Value& value) = 0;
    virtual Value get(const Key& key) = 0;
    virtual void remove(const Key& key) = 0;
    virtual void removeAll() = 0;
};

//注意： 子类继承时，必须全部实现，否则编译器会认为你的子类为纯虚类
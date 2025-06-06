#include <iostream>
#include <string>
#include "LruCache.hpp"

void testLruCache(){
    // 创建一个容量为 3 的 LRU 缓存，Key 为 string，Value 为 int
    LruCache<std::string, int> cache(3);

    // put 操作
    cache.put("one", 1);
    cache.put("two", 2);
    cache.put("three", 3);

    // 访问“one”，使其成为最近使用
    {
        int val;
        if (cache.get("one", val)) {
            std::cout << "get(one) = " << val << "\n";  // 应输出 1
        } else {
            std::cout << "one 未命中\n";
        }
    }

    // 再插入第四个元素，触发驱逐最旧条目
    // 当前链表顺序（最旧→最近）: two -> three -> one
    cache.put("four", 4);
    // 此时应驱逐“two”，因为它是最旧未访问的

    // 验证“two”是否被驱逐
    {
        int val;
        bool ok = cache.get("two", val);
        std::cout << "二次 get(two)：";
        if (!ok) {
            std::cout << "未命中（已被驱逐）\n";
        } else {
            std::cout << "命中! val = " << val << "\n";
        }
    }

    // 打印其余三个键的值，验证它们依然存在
    for (auto key : { "one", "three", "four" }) {
        int val;
        if (cache.get(key, val)) {
            std::cout << "get(" << key << ") = " << val << "\n";
        } else {
            std::cout << key << " 未命中\n";
        }
    }

    // 再次插入一个新键 “five”，此时链表（最旧→最近）应为:
    // three -> one -> four
    // 插入 five 会驱逐最旧“three”
    cache.put("five", 5);

    // 验证“three”是否被驱逐
    {
        int val;
        bool ok = cache.get("three", val);
        std::cout << "再一次 get(three)：";
        if (!ok) {
            std::cout << "未命中（已被驱逐）\n";
        } else {
            std::cout << "命中! val = " << val << "\n";
        }
    }

    // 最终缓存应包括： one, four, five（访问顺序从最旧到最近）
    std::cout << "最终缓存内容：\n";
    for (auto key : { "one", "four", "five" }) {
        int val;
        cache.get(key, val);  // get 会把 key 移到最近，但这里只是拿值
        std::cout << "  " << key << " = " << val << "\n";
    }
}

int main() {
    
    testLruCache();
    return 0;
}

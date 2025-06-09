#include <iostream>
#include "LruKCache.hpp"

void testLruKCacheFeature(){
    LruKCache<std::string, int> cache(3,3,3);;

    // put 操作
    cache.put("one", 1);
    cache.put("two", 2);
    cache.put("three", 3);

    // 访问"one"
    {
        int val;
        if (cache.get("one", val)) {
            std::cout << "get(one) = " << val << "\n";
        } else {
            std::cout << "one 未命中\n";
        }
    }

    // 访问"one"
    {
        int val;
        if (cache.get("one", val)) {
            std::cout << "get(one) = " << val << "\n";
        } else {
            std::cout << "one 未命中\n";
        }
    }

    cache.put("two", 22);
    // 访问"two"
    {
        int val;
        if (cache.get("two", val)) {
            std::cout << "get(two) = " << val << "\n";
        } else {
            std::cout << "two 未命中\n";
        }
    }

}


int main() {    
    // std::cout << "===== 开始功能测试 =====" << std::endl;
    // testLruCacheFeature();
    
    // std::cout << "\n\n===== 开始性能测试 =====" << std::endl;
    // testLruCachePerformance();

    testLruKCacheFeature();

    
    return 0;
}
#include <iostream>
#include "testLru.hpp"


int main() {    
    std::cout << "===== 开始功能测试 =====" << std::endl;
    testLruCacheFeature();
    
    std::cout << "\n\n===== 开始性能测试 =====" << std::endl;
    testLruCachePerformance();
    
    return 0;
}
参考博客：
- [缓存介绍](https://blog.csdn.net/chongfa2008/article/details/121956961)
- [LRU算法](https://blog.csdn.net/saxon_li/article/details/123974696)
- [LFU算法](https://blog.csdn.net/saxon_li/article/details/123985667)
- [ARC算法](https://blog.csdn.net/m0_73111380/article/details/142534135)


## LRU（Least Recently Used，最近最少使用）

  * **维护的数据结构**

    * `DList`：一个双向链表，按访问时间从 MRU（尾部）到 LRU（头部）排序。
    * `Map`：键到链表节点的哈希表，用于 O(1) 查找、插入、删除。
  * **容量**

    * **`C`**：缓存可容纳的最大块数量。
  * **访问流程**

    1. **缓存命中 (`x ∈ Map`)**

       * 访问到的节点 `x` 从链表中摘除，重新插入到尾部（MRU）。
       * 返回对应数据。
    2. **缓存未命中 (`x ∉ Map`)**

       * 若 `|DList| == C`：

         * 从链表头部（LRU 端）移除最旧节点 `y`，并从 `Map` 中删除 `y.key`。
       * 从后端加载数据，创建新节点 `x`，插入到链表尾部（MRU），并在 `Map` 中添加 `x.key → x`。
       * 返回对应数据。
  * **替换策略**

    * **始终抛弃最近最少访问（LRU）**：当容量满时，只需要一次 O(1) 操作（移除头部节点）即可腾出空间。

## LFU（Least Frequently Used，最不常用）

  * **维护的数据结构**

    * `FreqMap`：频次到“同频次链表”的哈希表，每个链表按访问时间从 MRU 到 LRU 排序。
    * `NodeMap`：键到节点结构的哈希表，节点包含 `{ key, value, freq }`。
    * `minFreq`：当前缓存中最小的访问频次。
  * **容量**

    * **`C`**：缓存可容纳的最大块数量。
  * **访问流程**

    1. **缓存命中 (`x ∈ NodeMap`)**

       * 取出节点 `x`，从 `FreqMap[x.freq]` 链表中移除；
       * 若该链表为空且 `x.freq == minFreq`，则 `minFreq++`；
       * `x.freq++`，插入到 `FreqMap[x.freq]` 链表的尾部（MRU）；
       * 更新 `NodeMap[x.key] = x`，返回数据。
    2. **缓存未命中 (`x ∉ NodeMap`)**

       * 若 `|NodeMap| == C`：

         * 在 `FreqMap[minFreq]` 的头部（LRU）移除最旧节点 `y`；
         * 从 `NodeMap` 中删除 `y.key`。
       * 加载新数据，创建节点 `x`（`freq = 1`），插入到 `FreqMap[1]` 的尾部（MRU）；
       * 在 `NodeMap` 中添加 `x.key → x`，并将 `minFreq = 1`；
       * 返回数据。
  * **替换策略**

    * **抛弃最少访问**：总是从当前最小频次的节点链表里，淘汰最旧（LRU 端）的节点，从而同时兼顾“最少使用”和“旧”两个维度。



## ARC（Adaptive Replacement Cache，动态替换缓存）

* **维护四条链表**

  * `T1`（最近一次访问的“新”数据，LRU 策略）
  * `T2`（被多次访问的“老”数据，LFU 效果，由频繁访问推动）
  * `B1`（`T1` 淘汰出去的 key 的“幽灵”链表）
  * `B2`（`T2` 淘汰出去的 key 的“幽灵”链表）

* **自适应参数 `p`**

  * 表示当前倾向于给 `T1`（新数据）分配的容量份额，初始 `p = 0`，限制在 `[0, C]` 之间。

* **访问流程**

  1. **缓存命中（`x ∈ T1 ∪ T2`）**

     * 如果 `x ∈ T1`：

       * 将 `x` 从 `T1` 移动到 `T2` 的 MRU 端（表示此块已经“频繁”访问）
     * 如果 `x ∈ T2`：

       * 直接将 `x` 在 `T2` 中调整到 MRU 端
     * 返回数据。

  2. **幽灵命中**

     * **`x ∈ B1`（命中 `B1`）**：

       * 增加对“新数据”侧的偏好：

         ```
         p ← min(p + max(|B2|/|B1|, 1), C)
         ```
       * 调用 `Replace()` 释放一个槽位
       * 将 `x` 从 `B1` 移除，插入到 `T2` 的 MRU 端
     * **`x ∈ B2`（命中 `B2`）**：

       * 增加对“老数据”侧的偏好：

         ```
         p ← max(p - max(|B1|/|B2|, 1), 0)
         ```
       * 调用 `Replace()`
       * 将 `x` 从 `B2` 移除，插入到 `T2` 的 MRU 端

  3. **缓存未命中 && 不在幽灵表中**

     * 如果 `|T1| + |T2| == C`：（列表容量为 `C`）

       * 调用 `Replace()`
     * 否则如果 `|T1| + |T2| < C` 且 `|T1| + |B1| + |T2| + |B2| ≥ C`：

       * 若 `|T1| + |B1| == C`，则删除 `B1` 的 LRU；否则删除 `B2` 的 LRU
     * 将新块 `x` 插入 `T1` 的 MRU 端

* **替换函数 `Replace()`**

  ```text
  如果 |T1| ≥ 1 且（|T1| > p 或 x ∈ B2 且 |T1| == p）：
      将 T1 的 LRU 块“淘汰”到 B1 的 MRU 端
  否则：
      将 T2 的 LRU 块“淘汰”到 B2 的 MRU 端
  如果相应的 ghost 链表超过容量 C，删除其 LRU 元素
  ```

* **容量与淘汰**

  * `T1` + `T2` 的总大小 ≤ `C`
  * `B1` + `B2` 的总大小 ≤ `C`（插入时若超出，删除最旧幽灵）

---


# HeapAllocator
HeapAllocator is a high performance heap allocator which can be use to alloc memory more efficiently as well as checking your memory leak through this tool. You can check allcoation information if you enabled DEBUG_ALLOCATOR macro, there are some extra debug info which may be able to help you know better about your memory using.

There are two categories of allocation blocks here: 
1. small block, means the size you ask from system is not larger than 2^8 bytes. Small blocks are stored in bucket structure.
2. large block, on the other side, means the size you ask from system is larger than 2^8 bytes. Large blocks are stored in rbtree structure.

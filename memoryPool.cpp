#include "memoryPool.h"
#include <cassert>
namespace memoryPool {
    //构造函数，初始化内存池
    MemoryPool::MemoryPool(size_t BlockSize)
    : BlockSize_ (BlockSize), SlotSize_ (0), firstBlock_ (nullptr),
      curSlot_ (nullptr), freeList_ (nullptr), lastSlot_ (nullptr){}

    //析构函数
    //释放一个内存池，即把链表连接的每一块内存块释放
    MemoryPool::~MemoryPool() {
        //先存下链表表头
        Slot* cur = firstBlock_;
        while (cur) {
            Slot* next = cur->next;
            //使用operator delete而不是delete，因为只需要释放内存块，不需要调用析构函数
            //我们最开始就没有创建任何对象，只是借了一块空间
            operator delete(reinterpret_cast<void*>(cur));
            cur = next;
        }
    }

    //初始化
    void MemoryPool::init(size_t size) {
        //断言，防止出现size<0的情况
        assert(size > 0);
        SlotSize_ = size;
        firstBlock_ = nullptr;
        curSlot_ = nullptr;
        freeList_.store(nullptr, std::memory_order_relaxed);
        lastSlot_ = nullptr;
    }

    //内存空间分配
    void* MemoryPool::allocate() {
        //优先使用空闲链表中的内存槽
        Slot* slot = popFreeList();
        if (slot != nullptr) {
            return slot;
        }

        //若空闲链表不可用，分配新的内存
        //上锁
        std::lock_guard<std::mutex> lock(mutexForBlock_);
        if (curSlot_ >= lastSlot_) {
            allocateNewBlock();
        }

        //更新内存槽使用情况
        Slot* result = curSlot_;
        curSlot_ = reinterpret_cast<Slot*>(
            reinterpret_cast<char*>(curSlot_) + SlotSize_
        );
        return result;
    }

    //内存空间回收
    void MemoryPool::deallocate(void * ptr) {
        //空指针检查
       if (ptr) {
           return;
       }
        Slot* slot = static_cast<Slot*>(ptr);
        pushFreeList(slot);
    }

    //申请新的内存块
    void MemoryPool::allocateNewBlock() {
        //申请一块大小为BlockSize_的内存
        void* newBlock = operator new(BlockSize_);
        //强制转换为Slot*类型后插入到内存块的链表中，并更新头指针
        reinterpret_cast<Slot*>(newBlock)->next = firstBlock_;
        firstBlock_ = reinterpret_cast<Slot*>(newBlock);

        //计算实际分配给用户的内存空间的起始位置
        //因为newBlock这块空间，实际上有一部分用于存储Slot*类型的指针，所以要跳过这一部分
        char* body = reinterpret_cast<char*>(newBlock) + sizeof(Slot*);
        //计算对齐所需的步数
        size_t paddingSize = padPointer(body, SlotSize_);
        //更新内存槽使用情况，确定下一次分配的起始点
        curSlot_ = reinterpret_cast<Slot*>(body + paddingSize);

        //更新边界
        //计算方式为：当前头指针地址 + 内存块大小 - 内存槽大小（为了确保最后有一个完整的内存槽） + 1（配合上面>=的判断逻辑）
        lastSlot_ = reinterpret_cast<Slot*>(reinterpret_cast<size_t>(newBlock) + BlockSize_ - SlotSize_ + 1);

        freeList_ = nullptr;
    }

    //计算对齐所需补充字节数
    size_t MemoryPool::padPointer(char *p, size_t align) {
        return (align - reinterpret_cast<size_t>(p) % align) % align;
    }

    void HashBucket::initMemoryPool() {
        for (int i = 0; i < MEMORY_POOL_NUM; i++) {
            getMemoryPool(i).init((i + 1) * SLOT_BASE_SIZE);
        }
    }

    MemoryPool &HashBucket::getMemoryPool(int index) {
        static MemoryPool memoryPool[MEMORY_POOL_NUM];
        return memoryPool[index];
    }

    //实现无锁入队
    bool MemoryPool::pushFreeList(Slot *slot) {
        //循环确保线程在CAS不成功的情况下，可以一直重试
        while (true) {
            //读取当前头指针，存入oldHead
            Slot* oldHead = freeList_.load(std::memory_order_relaxed);
            //将slot插入到头指针前
            slot->next.store(oldHead, std::memory_order_relaxed);
            //CAS操作：如果freeList_依然指向oldHead，则将其更新为slot
            if (freeList_.compare_exchange_weak(oldHead, slot,
                                            //memory_order_release 确保在 slot->next 赋值完成后，头指针的更新才对其他线程可见
                                            //若成功，返回 true
                                            std::memory_order_release, 
                                            //若失败（说明有其他线程竞争），自动更新oldHead为最新值并返回false
                                            std::memory_order_relaxed)) {
                return true;
            }
        }
    }

    //实现无锁出队
    Slot *MemoryPool::popFreeList() {
        //循环确保线程在CAS不成功的情况下，可以一直重试
        while (true) {
            //读取头指针
            Slot* oldHead = freeList_.load(std::memory_order_relaxed);
            //空指针检查
            if (oldHead == nullptr) {
                return nullptr;
            }

            //读取下一个指针，备用作为接下来的头指针
            Slot* newHead = oldHead->next.load(std::memory_order_relaxed);
            
            if (freeList_.compare_exchange_weak(oldHead, newHead,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                return oldHead;
            }
        }
    }
}

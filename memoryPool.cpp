#include "memoryPool.h"
#include <cassert>
namespace memoryPool {
    //构造函数，初始化内存池
    MemoryPool::MemoryPool(size_t BlockSize) : BlockSize_ (BlockSize){}

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
        freeList_ = nullptr;
        lastSlot_ = nullptr;
    }

    //内存空间分配
    void* MemoryPool::allocate() {
        //优先使用空闲链表中的内存槽
        if (freeList_ != nullptr) {
            //开启一个临时作用域，用于控制锁的生命周期
            {
                //把原始的mutex包装成一个智能的锁
                std::lock_guard<std::mutex> lock(mutexForFreeList_);
                //双重检查，加锁后再次确认空闲链表是否可以使用
                if (freeList_ != nullptr) {
                    //当前表头temp，也就是我们需要拿来用的内存
                    Slot* temp = freeList_;
                    freeList_ = freeList_->next;
                    return temp;
                }
            }
        }

        //若空闲链表不可用
        Slot* temp;
        //同上，开启一个临时作用域，用于控制锁的生命周期
        {
            std::lock_guard<std::mutex> lock(mutexForBlock_);
            //判断当前内存块还有没有内存槽可以使用
            if (curSlot_ >= lastSlot_) {
                //若没有，申请一块新空间
                allocateNewBlock();
            }

            temp = curSlot_;
            //更新内存槽使用情况
            curSlot_ += SlotSize_ / sizeof(Slot);
        }

        return temp;
    }

    //内存空间回收
    void MemoryPool::deallocate(void * ptr) {
        //空指针检查
       if (ptr) {
           std::lock_guard<std::mutex> lock(mutexForFreeList_);
           //将传进来的指针转换成可以放到空闲链表的Slot*类型，并将其插入空闲链表头部
           reinterpret_cast<Slot*>(ptr)->next = freeList_;
           //更新空闲链表头指针
           freeList_ = reinterpret_cast<Slot*>(ptr);
       }
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
}

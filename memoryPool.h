#include <iostream>
#include <mutex>

namespace memoryPool {
#define MEMORY_POOL_NUM 64                                              //内存池数量
#define SLOT_BASE_SIZE 8                                          //每个内存槽最小大小
#define MAX_SLOT_SIZE 512                                            //内存槽最大大小

    //内存槽结构
    struct Slot {
        Slot* next;
    };

    //内存池类
    class MemoryPool {
    public:
        MemoryPool(size_t BlockSize = 4096);                  //整个内存池的大小为4096
        ~MemoryPool();

        void init(size_t);                                              //初始化大小
        void* allocate();                                                 //分配内存
        void deallocate(void*);                                           //回收内存

    private:
        void allocateNewBlock();                                     //申请一块新空间
        size_t padPointer(char* p,size_t align);                          //对齐内存

    private:
        int BlockSize_;                                                  //内存块大小
        int SlotSize_;                                                      //槽大小
        Slot* firstBlock_;                                       //指向内存池管理的首个
        Slot* curSlot_;                                          //指向当前未被使用的槽
        Slot* freeList_;                                //指向空闲槽（被释放后又未被使用）
        Slot* lastSlot_;             //当前内存块最后未被使用的位置（若超过则需申请新的内存块）
        std::mutex mutexForFreeList_;                            //保护空闲链表的互斥锁
        std::mutex mutexForBlock_;                               //保护内存申请的互斥锁
    };

    //哈希桶类（用于存放多个内存池）
    class HashBucket {
    public:
        static void initMemoryPool(); //初始化内存池
        static MemoryPool& getMemoryPool(int index); //按索引获取内存池

        //内存分配
        static void* useMemory(size_t size) {
            //边界检查
            if (size <= 0) {
                return nullptr;
            }
            //若所需内存过大，直接向系统申请
            if (size > MAX_SLOT_SIZE) {
                //new在分配空间的同时还会调用构造函数，但这里我们只需要空间，所以使用operator new
                return operator new(size);
            }
            //小内存处理
            //因为每个内存池的槽大小都为SLOT_BASE_SIZE的倍数，且依次递增，故通过+7（向上取整）再除去这个基数寻找索引
            //-1是为了调整为从0开始的索引
            //找到需要的那个内存池，对它进行分配操作
            return getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).allocate();
        }

        //内存释放
        static void freeMemory(void* ptr, size_t size) {
            //空指针检查
            if (!ptr) {
                return;
            }
            //若这块内存过大，说明它并非来自内存池内，而是从系统申请而来，所以从哪里来，回哪里去
            if (size > MAX_SLOT_SIZE) {
                operator delete(ptr);
                return;
            }

            //同理，找到对应的内存池进行内存释放操作
            getMemoryPool(((size + 7) / SLOT_BASE_SIZE) - 1).deallocate(ptr);
        }

        //声明模板；"typename... args" 表示可以可以接受任意数量的参数
        template<typename T, typename... Args>
        //将newElement声明为友元函数
        friend T* newElement(Args&&... args);

        template<typename T>
        //同上
        friend void deleteElement(T* p);
    };

    //向内存池申请内存
    template<typename T, typename... Args>
    T* newElement (Args&&... args) {
        T* p = nullptr;
        //reinterpret_cast的作用是强制类型转换
        //将类型为void*的p强制转换为所需的T*的类型，通过大小分配内存；若它只是个空指针，则直接返回
        // 加上括号，先执行赋值，再比较
        if ((p = reinterpret_cast<T*>(HashBucket::useMemory(sizeof(T)))) != nullptr) {
            //new(p)是在已存在的内存地址p上调用构造函数，即定位new
            //forward的作用是完美转发，它会把传进来的参数以一模一样的类型和值类别（左值/右值）传递给T的构造函数
            new(p) T(std::forward<Args>(args)...);
        }
        return p;
    }

    //释放内存
    template <typename T>
    void deleteElement(T* p) {
        if (p) {
            //这样析构仅清理资源，不会把空间归还给系统
            p->~T();
            HashBucket::freeMemory(reinterpret_cast<void*>(p), sizeof(T));
        }
    }
}

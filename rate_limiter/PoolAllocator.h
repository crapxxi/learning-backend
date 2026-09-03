#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <mutex>

class PoolAllocator {
    private:
        struct Node {
            Node* next;
        };
        uint8_t* buffer;
        Node* head;
        size_t blockCount;
        size_t blockSize;
        std::mutex mtx_allocator;

    public:
        PoolAllocator(size_t objectSize, size_t blockCount) : blockCount(blockCount) {
            blockSize = std::max(objectSize, sizeof(Node));
            buffer = new uint8_t[blockSize * blockCount];

            head = reinterpret_cast<Node*>(buffer);
            Node* current = head;

            for(size_t i = 0; i < blockCount - 1; ++i) {
                uint8_t* nextAddress = reinterpret_cast<uint8_t*>(current)+blockSize;
                current->next = reinterpret_cast<Node*> (nextAddress);
                current = current->next;
            }

            current->next = nullptr;
        }

        PoolAllocator(const PoolAllocator&) = delete;
        PoolAllocator& operator=(const PoolAllocator&) = delete;

        ~PoolAllocator() {
            delete[] buffer;
        }

        void* allocate() {
            std::lock_guard<std::mutex> lock(mtx_allocator);
            if(head == nullptr) return nullptr;

            void *ptr = head;
            head = head->next;
            return ptr;
        }

        void deallocate(void* ptr) {
            if(!ptr) return;

            std::lock_guard<std::mutex> lock(mtx_allocator);
            Node* node = reinterpret_cast<Node*>(ptr);
            node->next = head;
            head=node;
        }
};
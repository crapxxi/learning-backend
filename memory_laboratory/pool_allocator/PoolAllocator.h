#include <cstddef>
#include <cstdint>
#include <algorithm>

class PoolAllocator {
    private:
        struct Node {
            Node* next;
        };
        uint8_t* buffer;
        Node* head;
        size_t blockCount;
        size_t blockSize;

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

        ~PoolAllocator() {
            delete[] buffer;
        }

        void* allocate() { 
            if(head == nullptr) return nullptr;

            void *ptr = head;
            head = head->next;
            return ptr;
        }

        void deallocate(void* ptr) {
            if(!ptr) return;

            Node* node = reinterpret_cast<Node*>(ptr);
            node->next = head;
            head=node;
        }
};
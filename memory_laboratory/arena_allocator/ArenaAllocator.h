#include <cstddef>
#include <cstdint>

class ArenaAllocator {
    private:
        uint8_t* buffer;
        size_t capacity;
        size_t offset;
    
    public:
        ArenaAllocator(size_t capacity) : capacity(capacity), offset(0) {
            buffer = new uint8_t[capacity];
        }

        ~ArenaAllocator() {
            delete[] buffer;
        }

        void* allocate(size_t size, size_t alignment) {
            size_t current_addr = reinterpret_cast<uintptr_t>(buffer + offset);

            size_t padding = (alignment - (current_addr & (alignment - 1)))&(alignment - 1);

            if(size + offset + padding > capacity) return nullptr;

            offset += padding;
            void* ptr = &buffer[offset];
            offset += size;

            return ptr;
        }

        void reset() {
            offset = 0;
        }
};

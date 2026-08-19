#include <iostream>
#include <stdexcept>
#include <cstddef>
#include <new>
#include <utility>

template<typename T, std::size_t Capacity> class FixedVector {
    std::size_t size_ = 0;
    alignas(alignof(T)) std::byte raw_buffer[sizeof(T) * Capacity];

    T* data() noexcept {
        return reinterpret_cast<T*>(raw_buffer);
    }

    const T* data() const noexcept {
        return reinterpret_cast<const T*>(raw_buffer);
    }

    void clear() noexcept {
        for(std::size_t i = 0; i < size_; i++) {
            data()[i].~T();
        }
        size_ = 0;
    }

    public:
        FixedVector() = default;

        FixedVector(const FixedVector&) = delete;
        FixedVector& operator=(const FixedVector&) = delete;

        FixedVector(FixedVector&& other) noexcept {
            for(std::size_t i = 0; i < other.size_; i++) {
                new (data() + i) T(std::move(other[i]));
            }

            size_ = other.size_;

            other.clear();
        }

        FixedVector& operator=(FixedVector&& other) {
            if(this!=&other){
                clear();
                
                for(std::size_t i = 0; i < other.size_; i++) {
                    new (data() + i) T(std::move(other[i]));
                }

                size_ = other.size_;

                other.clear();
            }

            return *this;
        }

        void push_back(const T& value) {
            if(size_ >= Capacity) throw std::out_of_range("fixed vector overflow");
            new (data() + size_) T(value);
            size_++;
        }

        void push_back(T&& value) {
            if(size_ >= Capacity) throw std::out_of_range("fixed vector overflow");
            new (data() + size_) T(std::move(value));
            size_++;
        }

        void pop_back() {
            if(size_ == 0) throw std::underflow_error("empty vector");
            size_--;
            data()[size_].~T();
        }

        T& operator[](std::size_t i) {
            return data()[i];
        }

        const T& operator[](std::size_t i) const {
            return data()[i];
        }

        std::size_t size() const {
            return size_;
        }

        bool empty() const {
            return size_ == 0;
        }

        ~FixedVector() {
            clear();
        }
};
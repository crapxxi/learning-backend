#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "PoolAllocator.h"
#include <chrono>
#include <stdexcept>
#include <cstring>
#include <string>
#include <shared_mutex>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

struct HashNode {
    char key[32]; // 32 byte
    char value[32]; // 32 byte
    uint64_t expire_at_ms; // 8 byte
    HashNode* next; // 8 byte
    // total 80 bytes
};

class FixedMiniDB {
    private:
        static constexpr size_t BUCKET_SIZE = 524288;
        static constexpr size_t NUM_SHARDS = 128;
        size_t max_records_;

        HashNode* buckets[BUCKET_SIZE] = {nullptr};
        PoolAllocator pool_;

        // multi thread
        std::array<std::shared_mutex, NUM_SHARDS> shard_mutexes_;
        
        // garbage collector
        std::condition_variable eviction_cv_;
        std::atomic<size_t> occupied_count_{0};
        std::mutex cv_mtx_;
        std::thread eviction_thread_;
        std::atomic<bool> stop_eviction_{false};
        

        uint64_t now_ms() const {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
        }

        size_t get_shard_index(int hash) const { return hash%NUM_SHARDS; }

        int hash(const char* key) {
            unsigned int h = 2166136261u;
            while(*key) {
                h ^= static_cast<unsigned int>(*key);
                h *= 16777619u;
                key++;
            }

            return h & 127;
        }

        // clear expired data

        void eviction_loop() {
            while(!stop_eviction_) {
                std::unique_lock<std::mutex> lock(cv_mtx_);

                eviction_cv_.wait_for(lock, std::chrono::seconds(5), [this]() {
                    return stop_eviction_.load();
                });

                if(stop_eviction_) break;
                lock.unlock();
                if (occupied_count_.load() > max_records_ / 2) {
                    run_active_eviction();
                }
            }
        }

        void run_active_eviction() {
            uint64_t current_time = now_ms();

            for(size_t shard = 0; shard < NUM_SHARDS; shard++) {
                std::unique_lock<std::shared_mutex> lock(shard_mutexes_[shard]);

                for(int b = shard; b < BUCKET_SIZE; b+=NUM_SHARDS) {
                    HashNode* prev = nullptr;
                    HashNode* current = buckets[b];

                    while(current != nullptr) {
                        if(current->expire_at_ms != 0 && current_time >= current->expire_at_ms) {
                            HashNode* to_del = current;
                            if(prev == nullptr) {
                                buckets[b] = current->next;
                                current = buckets[b];
                            } else {
                                prev->next = current->next;
                                current = current->next;
                            }
                            pool_.deallocate(to_del);
                            occupied_count_--;
                            
                        } else {
                            prev = current;
                            current = current->next;
                        }
                    }
                }
            }
        }

    public:
        FixedMiniDB(size_t max_records) : pool_(sizeof(HashNode), max_records), max_records_(max_records) { eviction_thread_ = std::thread(&FixedMiniDB::eviction_loop, this); }
        ~FixedMiniDB() {
            stop_eviction_ = true;
            eviction_cv_.notify_all();
            if(eviction_thread_.joinable()) eviction_thread_.join();
        }

        void put(const char* key, const char* value, uint64_t ttl) {
            int key_hash = hash(key); // hashing a key
            size_t shard = get_shard_index(key_hash);

            std::unique_lock<std::shared_mutex> lock(shard_mutexes_[shard]);

            HashNode* current = buckets[key_hash]; // head of the index

            while(current != nullptr) {
                if(std::strncmp(current->key, key, 32) == 0) {
                    std::strncpy(current->value, value, 31);
                    current->value[31] = '\0';
                    current->expire_at_ms = (ttl > 0) ? (now_ms() + ttl * 1000) : 0;
                    return;
                }
                current = current->next;
            }

            void* mem = pool_.allocate();
            if(mem == nullptr) throw std::runtime_error("Out of Memory");

            HashNode* new_node = new (mem) HashNode();
            std::strncpy(new_node->key, key, 31);
            new_node->key[31] = '\0';

            std::strncpy(new_node->value, value, 31);
            new_node->value[31] = '\0';

            new_node->expire_at_ms = (ttl > 0) ? (now_ms() + ttl * 1000) : 0;

            new_node->next = buckets[key_hash];
            buckets[key_hash] = new_node;
            size_t current_occupied = ++occupied_count_;

            if(current_occupied > max_records_ / 2) eviction_cv_.notify_one();
        }

        std::string get(const char* key) {
            int key_hash = hash(key);
            size_t shard = get_shard_index(key_hash);

            std::shared_lock<std::shared_mutex> lock(shard_mutexes_[shard]);

            HashNode* current = buckets[key_hash];

            while(current != nullptr) {
                if(std::strncmp(current->key, key, 32) == 0) {
                    if(current->expire_at_ms != 0 && now_ms() >= current->expire_at_ms) {
                        lock.unlock();
                        del(key);
                        return {};
                    }
                    return std::string(current->value);
                }
                current = current->next;
            }

            return {};
        }

        bool del(const char* key) {
            int key_hash = hash(key);
            size_t shard = get_shard_index(key_hash);

            std::unique_lock<std::shared_mutex> lock(shard_mutexes_[shard]);

            HashNode* prev = nullptr;
            HashNode* current = buckets[key_hash];

            while(current != nullptr) {
                if(std::strncmp(current->key, key, 32) == 0) {
                    if(prev == nullptr) {
                        buckets[key_hash] = current->next;
                    } else {
                        prev->next = current->next;
                    }
                    pool_.deallocate(current);
                    occupied_count_--;
                    return true;
                }
                prev = current;
                current = current->next;
            }
            return false;
        }

        uint64_t get_exp(const char* key) {
            int h = hash(key);
            size_t shard = get_shard_index(h);
            uint64_t now_ms_value = now_ms();

            std::unique_lock<std::shared_mutex> lock(shard_mutexes_[shard]);

            HashNode* current = buckets[h];

            while(current != nullptr) {
                if(std::strncmp(current->key, key, 32) == 0) {
                    if(current->expire_at_ms != 0 && now_ms_value >= current->expire_at_ms) {
                        lock.unlock();
                        del(key);
                        return 0;
                    }
                    return current->expire_at_ms - now_ms_value;
                }
                current = current->next;
            }
            return 0;
        }

        int64_t incr(const char* key, int64_t delta = 1, uint64_t ttl_sec = 0) {
            int h = hash(key);
            size_t shard = get_shard_index(h);

            std::unique_lock<std::shared_mutex> lock(shard_mutexes_[shard]);
            
            // get part
            HashNode* current = buckets[h];

            while(current != nullptr) {
                if(std::strncmp(current->key, key, 32) == 0) {
                    if(current->expire_at_ms != 0 && now_ms() >= current->expire_at_ms) {
                        snprintf(current->value, sizeof(current->value), "%lld", static_cast<long long> (delta));
                        current->expire_at_ms = (ttl_sec > 0) ? (now_ms() + ttl_sec * 1000) : 0;
                        return delta;
                    }
                    int64_t current_val = std::atoll(current->value);
                    current_val += delta;
                    snprintf(current->value, sizeof(current->value), "%lld", static_cast<long long> (current_val));

                    if(ttl_sec > 0) current->expire_at_ms = now_ms() + ttl_sec*1000;

                    return current_val;
                }
                current = current->next;
            }

            void* mem = pool_.allocate();

            if(mem == nullptr) throw std::runtime_error("Out of memory");

            HashNode* new_node = new (mem) HashNode();
            std::strncpy(new_node->key, key, 31);
            new_node->key[31] = '\0';

            snprintf(new_node->value, sizeof(new_node->value), "%lld", static_cast<long long> (delta));
            new_node->expire_at_ms = (ttl_sec > 0) ? (now_ms() + ttl_sec * 1000) : 0;

            new_node->next = buckets[h];
            buckets[h] = new_node;

            size_t current_occupied = ++occupied_count_;
            if(current_occupied > max_records_ / 2) eviction_cv_.notify_one();

            return delta;
        }


};
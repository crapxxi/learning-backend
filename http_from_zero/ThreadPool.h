#pragma once

#include <thread>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>

class ThreadPool {
    private:
        std::vector<std::thread> workers;
        std::mutex mtx;
        std::condition_variable cv;
        std::atomic<bool> stop{false};
        std::queue<std::function<void()>> tasks;
    
    public:
        ThreadPool(size_t workers_count){
            for(int i = 0; i < workers_count; i++) {
                workers.emplace_back([this]() {
                    while(true) {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(this->mtx);

                            this->cv.wait(lock, [this]() { return this->stop || !this->tasks.empty();});

                            if(this->stop && this->tasks.empty()) return;

                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }

                        task();
                    }
                });
            }
        }

        void enqueue(std::function<void()> func) {
            {
                std::unique_lock<std::mutex> lock(mtx);
                if(stop) return;
                tasks.push(func);
            }
            cv.notify_one();
        }

        ~ThreadPool() {
            {
                std::unique_lock<std::mutex> lock(mtx);
                stop = true;
            }
            cv.notify_all();
            
            for(auto& worker : workers) {
                if(worker.joinable()) worker.join();
            }
        }
};
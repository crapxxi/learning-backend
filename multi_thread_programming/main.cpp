#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>

std::condition_variable cv_workers;
std::condition_variable cv_q_pusher;
std::mutex mtx_q;
std::mutex mtx_cout;

int id = 0;
bool new_clients = false;
bool done = false;

std::queue<int> clients;

void q_pusher_thread() {
    std::unique_lock<std::mutex> lck_q(mtx_q);
    cv_q_pusher.wait(lck_q, [] {return new_clients;});
    for(int i = 0; i < 10; i++)
        clients.push(++id);
    cv_workers.notify_all();
}

void worker_thread() {
    while(true) {
        int client_id = 0;

        {
            std::unique_lock<std::mutex> lck_q(mtx_q);

            cv_workers.wait(lck_q, [] {return !clients.empty() || done;});

            if(clients.empty() && done) return;

            if(clients.empty()) continue;


            client_id = clients.front();
            clients.pop();
        }

        {
            std::lock_guard<std::mutex> lck_cout(mtx_cout);
            std::cout << "Client: " << client_id << " is processed by thread: " << std::this_thread::get_id() << "\n";
        }
    }
}

int main() {
    std::vector<std::thread> workers;

    for(int i = 1; i <= 4; i++) {
        workers.emplace_back(worker_thread);
    }

    std::thread thrd_q(q_pusher_thread);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    {
        std::lock_guard<std::mutex> lock(mtx_q);
        new_clients = true;
    }
    cv_q_pusher.notify_one();

    thrd_q.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    {
        std::lock_guard<std::mutex> lock(mtx_q);
        done = true;
    }

    cv_workers.notify_all();

    for(auto& w : workers) {
        w.join();
    }

    std::cout << "Program is finished\n";

    return 0;
}
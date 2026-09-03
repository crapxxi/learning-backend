#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <random>

const char* SERVER_IP = "127.0.0.1";
int SERVER_PORT = 4733;

const int NUM_THREADS = 100;           
const int REQUESTS_PER_THREAD = 20000;   

std::atomic<long long> success_count(0);
std::atomic<long long> limited_count(0);
std::atomic<long long> error_count(0);

void heavy_stress_worker(int thread_id) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        error_count += REQUESTS_PER_THREAD;
        return;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        error_count += REQUESTS_PER_THREAD;
        close(sock);
        return;
    }

    for (int i = 0; i < REQUESTS_PER_THREAD; i++) {
        int user_id = (i % 2 == 0) ? (i % 10) : (thread_id * 100 + (i % 50));
        std::string cmd = "limit user:" + std::to_string(user_id) + " 50 50\r\n";

        ssize_t sent = send(sock, cmd.c_str(), cmd.length(), 0);
        if (sent < 0) {
            error_count++;
            close(sock);
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0 || connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                error_count += (REQUESTS_PER_THREAD - i - 1);
                break;
            }
            continue;
        }

        char buffer[4096] = {0};
        ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            if (strstr(buffer, "REJECT") != nullptr) {
                limited_count++;
            } else if (strstr(buffer, "ACCEPT") != nullptr) {
                success_count++;
            } else {
                error_count++;
            }
        } else {
            error_count++;
            break;
        }
    }

    close(sock);
}

int main() {
    std::cout << "=== RUNNING HEAVY STRESS TEST (C++17) ===\n"
              << "Threads: " << NUM_THREADS << "\n"
              << "Requests/Thread: " << REQUESTS_PER_THREAD << "\n"
              << "Total Requests: " << (NUM_THREADS * REQUESTS_PER_THREAD) << "\n\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(heavy_stress_worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    long long total_reqs = success_count.load() + limited_count.load() + error_count.load();
    double rps = (duration > 0) ? ((double)total_reqs * 1000.0 / duration) : 0.0;

    std::cout << "\n--- Heavy Test Results ---\n";
    std::cout << "Total time: " << duration << " ms\n";
    std::cout << "Throughput: ~" << static_cast<long long>(rps) << " req/sec (RPS)\n";
    std::cout << "Successful (ACCEPT): " << success_count.load() << "\n";
    std::cout << "Limited (REJECT): " << limited_count.load() << "\n";
    std::cout << "Errors / Drops: " << error_count.load() << "\n";

    return 0;
}
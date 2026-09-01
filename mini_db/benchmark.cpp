#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <iomanip>

struct BenchResult {
    uint64_t total_requests = 0;
    uint64_t errors = 0;
    std::vector<double> latencies_ms;
};

// Формирует смешанную нагрузку (80% SET, 20% INCR)
std::string build_pipeline_payload(int batch_size, bool is_redis, int worker_id) {
    std::string payload;
    payload.reserve(batch_size * 48);

    for (int i = 0; i < batch_size; ++i) {
        int key_id = (worker_id + i) % 1000;
        std::string key = "key:" + std::to_string(key_id);
        std::string val = "val:" + std::to_string(i);

        if (i % 5 == 0) { // 20% INCR
            if (is_redis) {
                payload += "*2\r\n$4\r\nINCR\r\n$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
            } else {
                payload += "INCR " + key + "\r\n";
            }
        } else { // 80% SET
            if (is_redis) {
                payload += "*3\r\n$3\r\nSET\r\n$" + std::to_string(key.size()) + "\r\n" + key + 
                           "\r\n$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
            } else {
                payload += "SET " + key + " " + val + "\r\n";
            }
        }
    }
    return payload;
}

bool read_responses(int sock, int expected_responses, std::vector<char>& rx_buf) {
    int received_responses = 0;
    while (received_responses < expected_responses) {
        ssize_t n = read(sock, rx_buf.data(), rx_buf.size());
        if (n <= 0) return false;

        for (ssize_t i = 0; i < n; ++i) {
            if (rx_buf[i] == '\n') {
                received_responses++;
            }
        }
    }
    return true;
}

void client_worker(const std::string& host, int port, int conns_per_thread, 
                   int requests_per_conn, int batch_size, bool is_redis, 
                   int worker_offset, BenchResult& res) {
    std::string payload = build_pipeline_payload(batch_size, is_redis, worker_offset);
    std::vector<char> rx_buf(131072);

    for (int c = 0; c < conns_per_thread; ++c) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        
        struct timeval tv{3, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            res.errors += requests_per_conn;
            close(sock);
            continue;
        }

        int batches = requests_per_conn / batch_size;

        for (int b = 0; b < batches; ++b) {
            auto start = std::chrono::high_resolution_clock::now();

            if (write(sock, payload.data(), payload.size()) <= 0) {
                res.errors += batch_size;
                break;
            }

            if (!read_responses(sock, batch_size, rx_buf)) {
                res.errors += batch_size;
                break;
            }

            auto end = std::chrono::high_resolution_clock::now();
            double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

            res.latencies_ms.push_back(elapsed_ms);
            res.total_requests += batch_size;
        }
        close(sock);
    }
}

void run_test(const std::string& name, const std::string& host, int port, 
              int threads_cnt, int total_conns, int total_reqs, int batch_size, bool is_redis) {
    std::cout << "\n--------------------------------------------------" << std::endl;
    std::cout << ">>> " << name << " | Batch: " << batch_size << " | Conns: " << total_conns << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    std::vector<std::thread> threads;
    std::vector<BenchResult> results(threads_cnt);
    int conns_per_thread = total_conns / threads_cnt;
    int reqs_per_conn = total_reqs / total_conns;

    auto t_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < threads_cnt; ++i) {
        threads.emplace_back(client_worker, host, port, conns_per_thread, 
                             reqs_per_conn, batch_size, is_redis, i * 13, std::ref(results[i]));
    }

    for (auto& t : threads) t.join();

    auto t_end = std::chrono::high_resolution_clock::now();
    double total_time_sec = std::chrono::duration<double>(t_end - t_start).count();

    uint64_t grand_total_reqs = 0;
    uint64_t total_errors = 0;
    std::vector<double> all_latencies;

    for (const auto& r : results) {
        grand_total_reqs += r.total_requests;
        total_errors += r.errors;
        all_latencies.insert(all_latencies.end(), r.latencies_ms.begin(), r.latencies_ms.end());
    }

    if (all_latencies.empty()) {
        std::cout << " [!] Тест завершился с ошибкой. Ответов не получено (Ошибок: " << total_errors << ")\n";
        return;
    }

    std::sort(all_latencies.begin(), all_latencies.end());

    double rps = grand_total_reqs / total_time_sec;
    size_t sz = all_latencies.size();

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Время выполнения:  " << total_time_sec << " сек\n";
    std::cout << "Успешных запросов: " << grand_total_reqs << "\n";
    std::cout << "Ошибок:            " << total_errors << "\n";
    std::cout << "Производительность: " << (long)rps << " RPS\n";
    std::cout << "Latency p50:       " << all_latencies[sz * 0.50] << " ms\n";
    std::cout << "Latency p95:       " << all_latencies[sz * 0.95] << " ms\n";
    std::cout << "Latency p99:       " << all_latencies[sz * 0.99] << " ms\n";
    std::cout << "Latency p99.9:     " << all_latencies[sz * 0.999] << " ms\n";
}

int main() {
    int threads = 8;
    int connections = 128;
    int minidb_port = 4733;
    int redis_port = 6379;

    std::cout << "==================================================" << std::endl;
    std::cout << "         HARDCORE BENCHMARK: FixedMiniDB vs Redis" << std::endl;
    std::cout << "==================================================" << std::endl;

    // 1. Чистая латентность сетевого стека (Batch = 1)
    run_test("FixedMiniDB (Batch=1)", "127.0.0.1", minidb_port, threads, connections, 500000, 1, false);
    run_test("Redis (Batch=1)", "127.0.0.1", redis_port, threads, connections, 500000, 1, true);

    // 2. Высокая пропускная способность (Batch = 64)
    run_test("FixedMiniDB (Batch=64)", "127.0.0.1", minidb_port, threads, connections, 10000000, 64, false);
    run_test("Redis (Batch=64)", "127.0.0.1", redis_port, threads, connections, 10000000, 64, true);

    // 3. Предельная нагрузка (Batch = 128)
    run_test("FixedMiniDB (Batch=128)", "127.0.0.1", minidb_port, threads, connections, 20000000, 128, false);
    run_test("Redis (Batch=128)", "127.0.0.1", redis_port, threads, connections, 20000000, 128, true);

    return 0;
}
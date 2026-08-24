#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <map>
#include "HttpParser.h"
#include "ThreadPool.h"
#include <mutex>

std::mutex output_mtx;

void print_request(HttpRequest& req) {
    std::lock_guard<std::mutex> lock(output_mtx);
    std::cout << "\n--- Query ---\n";
    std::cout << "Method:  " << req.requestLine.method << "\n";
    std::cout << "URI:     " << req.requestLine.URI << "\n";
    std::cout << "Version: " << req.requestLine.version << "\n";
        
    std::cout << "\nHeaders:\n";
    for (const auto& [key, val] : req.headers) {
        std::cout << "  " << key << ": " << val << "\n";
    }

    std::string body(req.body.begin(), req.body.end());
    std::cout << "\nBody (" << req.body.size() << " bytes): " << body << "\n";
}

int main() {
    ThreadPool thread_pool(10);

    int server = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server, (sockaddr*)&addr, sizeof(addr));

    listen(server, 5);

    while(true) {
        int client = accept(server, NULL, NULL);

        if (client < 0) continue;

        thread_pool.enqueue([client]() {
            HttpParser parser;
            uint8_t buf[256];

            while(true) {
                ssize_t read_data = read(client, buf, sizeof(buf) - 1);

                if(read_data <= 0) {
                    close(client);
                    return;
                }

                ParseStatus status = parser.parse(buf, read_data);

                if(status == ParseStatus::Complete) {
                    HttpRequest req = parser.getRequest();
                    print_request(req);

                    write(client, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nOK\n", 41);
                    close(client);
                    return;
                } else if (status == ParseStatus::Error) {
                    const char* err_resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 15\r\nConnection: close\r\n\r\nBad Request\n";
                    write(client, err_resp, strlen(err_resp));
                    close(client);
                    return;
                }
            }
        });
    }

    return 0;
}
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>

int main() {
    int client = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);

    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(client, (sockaddr*)&addr, sizeof(addr));

    std::string chunk1 = "POST /api/data HTTP/1.1\r\nHost: example.com\r\nContent-Type: text/plain\r\nContent-";
    std::string chunk2 = "Length: 13\r\n\r\nHello, World!";

    send(client, reinterpret_cast<const uint8_t*>(chunk1.data()), chunk1.size(), 0);
    send(client, reinterpret_cast<const uint8_t*>(chunk2.data()), chunk2.size(), 0);

    uint8_t response[256];

    ssize_t bytes = read(client, response, 256);

    if (bytes > 0) {
        std::cout << "Response:\n";
        std::cout.write(reinterpret_cast<char*>(response), bytes);
        std::cout << std::endl;
    }

    close(client);
    
    return 0;
}
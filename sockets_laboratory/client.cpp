#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>

const int BUFFER_SIZE = 100;

int main() {
    int client = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);

    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(client, (sockaddr*)&addr, sizeof(addr));

    while(true) {
        char request[BUFFER_SIZE];
        std::cout << "Please enter the message[q for exit]:\n";
        std::cin.getline(request, BUFFER_SIZE - 1);

        if(request[0] == 'q') break;

        send(client, request, BUFFER_SIZE - 1, 0);

        char response[BUFFER_SIZE] = {0};

        ssize_t bytes = read(client, response, BUFFER_SIZE - 1);

        if(bytes > 0) std::cout << "Response: " << response;

    }

    close(client);

    return 0;
}
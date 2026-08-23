#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>

int main() {
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

        while(true) {

            char buf[100] = {0};

            ssize_t read_data = read(client, buf, sizeof(buf) - 1);

            if (read_data <= 0) {
                std::cout << "Клиент отключился." << std::endl;
                break;
            }

            std::cout << "message: " << buf << std::endl;

            write(client, "OK\n", 3);
        }

        close(client);
    }

    close(server);

    return 0;
}
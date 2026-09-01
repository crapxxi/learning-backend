#include <iostream>
#include <sys/socket.h>
#include <sys/event.h>
#include <unistd.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unordered_map>
#include <string>
#include <string_view>
#include <charconv>
#include <cstring>
#include <cerrno>

#include "FixedMiniDB.h"

constexpr int PORT = 4733;
constexpr int MAX_EVENTS = 1024;
constexpr int BUF_SIZE = 4096;
constexpr int MAX_RECORDS = 10000;
constexpr size_t MAX_KEY_LEN = 64;

struct Connection {
    int fd;
    std::string read_buf;
    std::string write_buf;
};

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void modify_write_event(int kq, int fd, bool enable) {
    struct kevent ev;
    uint16_t flags = enable ? (EV_ADD | EV_ENABLE) : EV_DELETE;
    EV_SET(&ev, fd, EVFILT_WRITE, flags, 0, 0, NULL);
    kevent(kq, &ev, 1, NULL, 0, NULL);
}

void close_connection(int kq, int fd, std::unordered_map<int, Connection>& clients) {
    close(fd);
    clients.erase(fd);
}

std::string_view get_next_token(std::string_view& src) {
    while (!src.empty() && src.front() == ' ') {
        src.remove_prefix(1);
    }
    if (src.empty()) return {};

    auto space_pos = src.find(' ');
    std::string_view token;

    if (space_pos == std::string_view::npos) {
        token = src;
        src = {};
    } else {
        token = src.substr(0, space_pos);
        src.remove_prefix(space_pos + 1);
    }

    return token;
}

constexpr bool starts_with(std::string_view sv, std::string_view prefix) {
    return sv.size() >= prefix.size() && sv.compare(0, prefix.size(), prefix) == 0;
}

std::string process_single_command(std::string_view line, FixedMiniDB& db) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
        line.remove_suffix(1);
    }

    std::string_view cmd = get_next_token(line);
    if (cmd.empty()) return "";

    if (starts_with(cmd, "Host:") || starts_with(cmd, "User-Agent:") || starts_with(cmd, "Accept:")) {
        return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nHTTP protocol not supported\r\n";
    }
    

    if (cmd == "set" || cmd == "SET") {
        std::string_view key = get_next_token(line);
        std::string_view value = get_next_token(line);
        std::string_view ttl_str = get_next_token(line);

        if (key.empty() || value.empty() || ttl_str.empty())
            return "ERR wrong number of arguments for 'set'\r\n";

        uint64_t ttl = 0;
        auto [ptr, ec] = std::from_chars(ttl_str.data(), ttl_str.data() + ttl_str.size(), ttl);
        if (ec != std::errc{})
            return "ERR value is not an integer or out of range\r\n";

        try {
            std::string k(key), v(value);
            db.put(k.c_str(), v.c_str(), ttl);
            return "SET OK\r\n";
        } catch (const std::exception&) {
            return "ERR out of memory\r\n";
        }
    } else if (cmd == "get" || cmd == "GET") {
        std::string_view key = get_next_token(line);
        if (key.empty()) return "ERR wrong number of arguments for 'get'\r\n";

        if (key.size() > MAX_KEY_LEN)
            return "ERR key is too long\r\n";

        if (starts_with(key, "/") || line.find("HTTP/") != std::string_view::npos) {
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nThis is a Redis-like MiniDB server. Use netcat or TCP client.\r\n";
        }

        std::string k(key);
        std::string resp = db.get(k.c_str());
        return !resp.empty() ? "GET ${" + resp + "}\r\n" : "GET ${nil}\r\n";
    } else if (cmd == "incr" || cmd == "INCR") {
        std::string_view key = get_next_token(line);
        std::string_view delta_str = get_next_token(line);
        std::string_view ttl_sec_str = get_next_token(line);

        if (key.empty()) return "ERR wrong number of arguments for 'incr'\r\n";

        uint64_t delta = 1;
        uint64_t ttl_sec = 0;

        if (!delta_str.empty()) {
            auto [ptr, ec] = std::from_chars(delta_str.data(), delta_str.data() + delta_str.size(), delta);
            if (ec != std::errc{}) delta = 1;
        }

        if (!ttl_sec_str.empty()) {
            auto [ptr, ec] = std::from_chars(ttl_sec_str.data(), ttl_sec_str.data() + ttl_sec_str.size(), ttl_sec);
            if (ec != std::errc{}) ttl_sec = 0;
        }

        try {
            std::string k(key);
            int64_t res = db.incr(k.c_str(), delta, ttl_sec);
            return "INCR OK ${" + std::to_string(res) + "}\r\n";
        } catch (const std::exception&) {
            return "ERR out of memory\r\n";
        }
    } else if (cmd == "del" || cmd == "DEL") {
        std::string_view key = get_next_token(line);
        if (key.empty()) return "ERR wrong number of arguments for 'del'\r\n";

        std::string k(key);
        return db.del(k.c_str()) ? "DEL OK\r\n" : "DEL NOT\r\n";
    } else {
        return "ERR unknown command\r\n";
    }
}

void handle_accept(int server_fd, int kq, std::unordered_map<int, Connection>& clients) {
    while (true) {
        sockaddr_in addr{};
        socklen_t client_len = sizeof(addr);
        int client = accept(server_fd, (struct sockaddr*)&addr, &client_len);

        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }

        set_nonblocking(client);

        struct kevent ev;
        EV_SET(&ev, client, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
        if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
            close(client);
            continue;
        }

        clients[client] = Connection{client, "", ""};
    }
}

void handle_read(int client_fd, int kq, std::unordered_map<int, Connection>& clients, FixedMiniDB& db) {
    auto it = clients.find(client_fd);
    if (it == clients.end()) return;

    Connection& conn = it->second;

    if (conn.read_buf.size() > 65536) {
        close_connection(kq, client_fd, clients);
        return;
    }

    char buffer[BUF_SIZE];

    while (true) {
        ssize_t bytes = read(client_fd, buffer, sizeof(buffer));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            close_connection(kq, client_fd, clients);
            return;
        } else if (bytes == 0) {
            close_connection(kq, client_fd, clients);
            return;
        }

        conn.read_buf.append(buffer, static_cast<size_t>(bytes));
    }

    size_t pos = 0;
    while ((pos = conn.read_buf.find('\n')) != std::string::npos) {
        std::string_view line(conn.read_buf.data(), pos);
        
        std::string resp = process_single_command(line, db);
        if (!resp.empty())
            conn.write_buf += resp;

        conn.read_buf.erase(0, pos + 1);
    }

    if (!conn.write_buf.empty()) {
        ssize_t sent = write(client_fd, conn.write_buf.data(), conn.write_buf.size());
        if (sent > 0)
            conn.write_buf.erase(0, static_cast<size_t>(sent));

        if (!conn.write_buf.empty())
            modify_write_event(kq, client_fd, true);
    }
}

void handle_write(int client_fd, int kq, std::unordered_map<int, Connection>& clients) {
    auto it = clients.find(client_fd);
    if (it == clients.end()) return;

    Connection& conn = it->second;
    if (conn.write_buf.empty()) {
        modify_write_event(kq, client_fd, false);
        return;
    }

    ssize_t sent = write(client_fd, conn.write_buf.data(), conn.write_buf.size());
    if (sent > 0) {
        conn.write_buf.erase(0, static_cast<size_t>(sent));
    } else if (sent < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        close_connection(kq, client_fd, clients);
        return;
    }
    if (conn.write_buf.empty())
        modify_write_event(kq, client_fd, false);
}

int main() {
    FixedMiniDB fmdb(MAX_RECORDS);
    std::unordered_map<int, Connection> clients;

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    set_nonblocking(server);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(server, SOMAXCONN) < 0) { perror("listen"); return 1; }

    int kq = kqueue();
    if (kq < 0) { perror("kqueue"); return 1; }

    struct kevent kev;
    EV_SET(&kev, server, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) { perror("kevent register"); return 1; }

    std::cout << "Server listening on port " << PORT << "...\n";

    struct kevent event_list[MAX_EVENTS];

    while (true) {
        int nevents = kevent(kq, NULL, 0, event_list, MAX_EVENTS, NULL);
        if (nevents < 0) {
            if (errno == EINTR) continue;
            perror("kevent wait");
            break;
        }

        for (int i = 0; i < nevents; i++) {
            int fd = static_cast<int>(event_list[i].ident);
            uint16_t flags = event_list[i].flags;
            int16_t filter = event_list[i].filter;

            if (flags & EV_ERROR) {
                if (fd != server) close_connection(kq, fd, clients);
                continue;
            }

            if (fd == server) {
                handle_accept(server, kq, clients);
            } else {
                if (filter == EVFILT_READ) {
                    handle_read(fd, kq, clients, fmdb);
                }
                if (filter == EVFILT_WRITE && (flags & EV_EOF) == 0) {
                    handle_write(fd, kq, clients);
                }
                if (flags & EV_EOF) {
                    close_connection(kq, fd, clients);
                }
            }
        }
    }

    close(server);
    close(kq);
    return 0;
}
#pragma once
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <cctype>

struct RequestLine {
    std::string method;
    std::string URI;
    std::string version;
};

struct HttpRequest {
    RequestLine requestLine;
    std::multimap<std::string, std::string> headers;
    std::vector<uint8_t> body;
};

enum class ParseStatus {
    Incomplete,
    Complete,
    Error
};

class HttpParser {
public:
    enum class State {
        RequestLine,
        Headers,
        Body,
        Complete,
        Error
    };

    ParseStatus parse(const uint8_t* data, size_t size) {
        buffer_.insert(buffer_.end(), data, data + size);
        return processBuffer();
    }

    const HttpRequest& getRequest() const { return request_; }
    State getState() const { return state_; }

private:
    State state_ = State::RequestLine;
    HttpRequest request_;
    std::vector<uint8_t> buffer_;
    size_t contentLength_ = 0;

    ParseStatus processBuffer() {
        while (state_ != State::Complete && state_ != State::Error) {
            if (state_ == State::RequestLine) {
                if (!parseRequestLine()) break;
            }
            else if (state_ == State::Headers) {
                if (!parseHeaders()) break;
            }
            else if (state_ == State::Body) {
                if (!parseBody()) break;
            }
        }

        if (state_ == State::Complete) return ParseStatus::Complete;
        if (state_ == State::Error) return ParseStatus::Error;
        return ParseStatus::Incomplete;
    }

    bool parseRequestLine() {
        auto crlf = findCRLF();
        if (crlf == buffer_.end()) return false;

        std::string line(buffer_.begin(), crlf);
        buffer_.erase(buffer_.begin(), crlf + 2);
        size_t pos1 = line.find(' ');
        size_t pos2 = line.find(' ', pos1 + 1);

        if (pos1 == std::string::npos || pos2 == std::string::npos) {
            state_ = State::Error;
            return false;
        }

        request_.requestLine.method  = line.substr(0, pos1);
        request_.requestLine.URI     = line.substr(pos1 + 1, pos2 - pos1 - 1);
        request_.requestLine.version = line.substr(pos2 + 1);

        state_ = State::Headers;
        return true;
    }

    bool parseHeaders() {
        while (true) {
            auto crlf = findCRLF();
            if (crlf == buffer_.end()) return false;

            if (crlf == buffer_.begin()) {
                buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
                
                auto clIt = request_.headers.find("content-length");
                if (clIt != request_.headers.end()) {
                    try {
                        contentLength_ = std::stoul(clIt->second);
                    } catch (...) {
                        state_ = State::Error;
                        return false;
                    }
                }

                state_ = (contentLength_ > 0) ? State::Body : State::Complete;
                return true;
            }

            std::string line(buffer_.begin(), crlf);
            buffer_.erase(buffer_.begin(), crlf + 2);

            size_t colonPos = line.find(':');
            if (colonPos == std::string::npos) {
                state_ = State::Error;
                return false;
            }

            std::string key = line.substr(0, colonPos);
            std::string val = line.substr(colonPos + 1);

            trim(key);
            trim(val);

            std::transform(key.begin(), key.end(), key.begin(), ::tolower);

            request_.headers.insert({key, val});
        }
    }

    bool parseBody() {
        if (buffer_.size() < contentLength_) {
            return false;
        }

        request_.body.assign(buffer_.begin(), buffer_.begin() + contentLength_);
        buffer_.erase(buffer_.begin(), buffer_.begin() + contentLength_);
        state_ = State::Complete;
        return true;
    }

    std::vector<uint8_t>::iterator findCRLF() {
        for (auto it = buffer_.begin(); it != buffer_.end(); ++it) {
            if (*it == '\r' && (it + 1) != buffer_.end() && *(it + 1) == '\n') {
                return it;
            }
        }
        return buffer_.end();
    }

    void trim(std::string& s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
    }
};
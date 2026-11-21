#include "aiws/ipc_server.hpp"
#include "aiws/gui/main_window.hpp"

#include <QMetaObject>
#include <QString>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace aiws {

namespace {

constexpr std::uint32_t MAX_PATH_LENGTH = 1024 * 1024;

bool receive_all(int fd, void* data, std::size_t size) {
    auto* bytes = static_cast<unsigned char*>(data);
    std::size_t received = 0;

    while (received < size) {
        pollfd descriptor{fd, POLLIN, 0};

        const int ready = poll(&descriptor, 1, 5000);
        if (ready <= 0) {
            return false;
        }

        const ssize_t count = recv(fd, bytes + received,
                                   size - received, 0);

        if (count <= 0) {
            return false;
        }

        received += static_cast<std::size_t>(count);
    }

    return true;
}

bool send_all(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t sent = 0;

    while (sent < size) {
        const ssize_t count = send(fd, bytes + sent, size - sent,
                                   MSG_NOSIGNAL);

        if (count <= 0) {
            return false;
        }

        sent += static_cast<std::size_t>(count);
    }

    return true;
}

void send_response(int fd, std::uint8_t status,
                   const std::string& reason) {
    const std::uint32_t length =
        htonl(static_cast<std::uint32_t>(reason.size()));

    send_all(fd, &status, sizeof(status));
    send_all(fd, &length, sizeof(length));

    if (!reason.empty()) {
        send_all(fd, reason.data(), reason.size());
    }
}

bool readable_file(const std::string& path) {
    const int fd = open(path.c_str(), O_RDONLY);

    if (fd < 0) {
        return false;
    }

    struct stat info {};
    const bool valid = fstat(fd, &info) == 0 && S_ISREG(info.st_mode);

    close(fd);
    return valid;
}

} // namespace

IpcServer::IpcServer(const std::string& socket_path,
                     aiws::gui::MainWindow* window)
    : socket_path_(socket_path), window_(window) {}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    if (worker_.joinable()) {
        return false;
    }

    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);

    if (server_fd_ < 0) {
        return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;

    if (socket_path_.size() >= sizeof(address.sun_path)) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    std::strncpy(address.sun_path, socket_path_.c_str(),
                 sizeof(address.sun_path) - 1);

    unlink(socket_path_.c_str());

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (listen(server_fd_, 5) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        unlink(socket_path_.c_str());
        return false;
    }

    stopping_ = false;
    worker_ = std::thread(&IpcServer::run, this);

    return true;
}

void IpcServer::stop() {
    stopping_ = true;

    {
        std::lock_guard<std::mutex> lock(client_mutex_);

        if (active_client_fd_ >= 0) {
            shutdown(active_client_fd_, SHUT_RDWR);
        }
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }

    unlink(socket_path_.c_str());
}

void IpcServer::run() {
    while (!stopping_) {
        pollfd descriptor{server_fd_, POLLIN, 0};

        const int ready = poll(&descriptor, 1, 100);

        if (stopping_) {
            break;
        }

        if (ready <= 0 || !(descriptor.revents & POLLIN)) {
            continue;
        }

        const int client_fd = accept(server_fd_, nullptr, nullptr);

        if (client_fd < 0) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            active_client_fd_ = client_fd;
        }

        if (stopping_) {
            shutdown(client_fd, SHUT_RDWR);
        } else {
            std::uint32_t network_length = 0;

            if (receive_all(client_fd, &network_length,
                            sizeof(network_length))) {

                const std::uint32_t length = ntohl(network_length);

                if (length > 0 && length <= MAX_PATH_LENGTH) {
                    std::string path(length, '\0');

                    if (receive_all(client_fd, path.data(), length)) {
                        if (readable_file(path)) {
                            const QString qpath =
                                QString::fromUtf8(path.data(),
                                                  static_cast<qsizetype>(path.size()));

                            QMetaObject::invokeMethod(
                                window_,
                                [window = window_, qpath]() {
                                    window->add_document_file(qpath);
                                },
                                Qt::QueuedConnection);

                            send_response(client_fd, 0, "");
                        } else {
                            send_response(client_fd, 1,
                                          "File is not readable.");
                        }
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            active_client_fd_ = -1;
        }

        close(client_fd);
    }
}

} // namespace aiws

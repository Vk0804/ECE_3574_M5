#include <filesystem>
#include <system_error>

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <system_error>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

// CR1 - Use a unix domain socket 
//aiws_send - sends document path to already running
//AI Workspace application

//Socket() - creates the communication endpoint 
//connect() - connects the client to the server
//write() send() - trnasmits bytes 
//Close() - closes the connection


// Used to measure timeout accurately
using Clock = std::chrono::steady_clock;

// Handles socket timeouts
static bool wait_for(int fd, short events, Clock::time_point deadline) {
    while (true) {
        auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now()).count();

        if (remaining < 0) return false;

        pollfd pfd{fd, events, 0};
        int result = poll(&pfd, 1, static_cast<int>(remaining + 1));

        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;

        if (pfd.revents & events) return true;
        return false;
    }
}

// sends the complete request, even if the OS only sends part of it at a time
static bool send_all(int fd, const void* data, size_t size,
                     Clock::time_point deadline) {
    const char* bytes = static_cast<const char*>(data);
    size_t sent = 0;

    while (sent < size) {
        if (!wait_for(fd, POLLOUT, deadline)) return false;

        ssize_t n = send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);

        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) return false;

        sent += static_cast<size_t>(n);
    }

    return true;
}


//Reads the complete response
static bool receive_all(int fd, void* data, size_t size,
                        Clock::time_point deadline) {
    char* bytes = static_cast<char*>(data);
    size_t received = 0;

    while (received < size) {
        if (!wait_for(fd, POLLIN, deadline)) return false;

        ssize_t n = recv(fd, bytes + received, size - received, 0);

        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) return false;

        received += static_cast<size_t>(n);
    }

    return true;
}

//converts a number into the protocol's 4 byte format
//endian integer
//Socket is a byte stream. it does not inheritenly know where one 
// message ends and another begins
static void encode_length(uint32_t value, unsigned char output[4]) {
    output[0] = static_cast<unsigned char>((value >> 24) & 0xff);
    output[1] = static_cast<unsigned char>((value >> 16) & 0xff);
    output[2] = static_cast<unsigned char>((value >> 8) & 0xff);
    output[3] = static_cast<unsigned char>(value & 0xff);
}

//Convert those 4 bytes back into a number
static uint32_t decode_length(const unsigned char input[4]) {
    return (static_cast<uint32_t>(input[0]) << 24) |
           (static_cast<uint32_t>(input[1]) << 16) |
           (static_cast<uint32_t>(input[2]) << 8) |
            static_cast<uint32_t>(input[3]);
}

// determines where the application is listing
static std::string default_socket_path() {
    const char* user = std::getenv("USER");

    std::string identity =
        (user && *user) ? user : std::to_string(getuid());

    return "/tmp/aiws-" + identity + ".sock";
}

int main(int argc, char* argv[]) {
    std::string socket_path;
    std::string file_path;

    if (argc == 3 && std::string(argv[1]) == "add") {
        socket_path = default_socket_path();
        file_path = argv[2];
    } else if (argc == 5 &&
               std::string(argv[1]) == "--socket" &&
               std::string(argv[3]) == "add") {
        socket_path = argv[2];
        file_path = argv[4];
    } else {
        std::cerr << "Usage: aiws_send [--socket PATH] add FILE\n";
        return 2;
    }

    std::error_code path_error;
    std::filesystem::path absolute_path =
    std::filesystem::absolute(file_path, path_error);

    if (path_error) {
        std::cerr << "Could not resolve file path.\n";
         return 2;
    }

    file_path = absolute_path.string();

    if (file_path.empty() ||
        file_path.size() > std::numeric_limits<uint32_t>::max()) {
        std::cerr << "Invalid file path.\n";
        return 2;
    }

    if (socket_path.empty() ||
        socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        std::cerr << "Invalid socket path.\n";
        return 2;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Could not create socket: "
                  << std::strerror(errno) << '\n';
        return 2;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Could not configure socket.\n";
        close(fd);
        return 2;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(),
                socket_path.size() + 1);

    int result = connect(
        fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));

    if (result < 0 && errno != EINPROGRESS) {
        std::cerr << "No application reachable: "
                  << std::strerror(errno) << '\n';
        close(fd);
        return 2;
    }

    auto connect_deadline = Clock::now() + std::chrono::seconds(2);

    if (result < 0) {
        if (!wait_for(fd, POLLOUT, connect_deadline)) {
            std::cerr << "Connection timed out.\n";
            close(fd);
            return 2;
        }

        int error = 0;
        socklen_t error_size = sizeof(error);

        if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
                       &error, &error_size) < 0 || error != 0) {
            std::cerr << "Could not connect to application.\n";
            close(fd);
            return 2;
        }
    }

    auto communication_deadline =
        Clock::now() + std::chrono::seconds(5);

    uint32_t path_length = static_cast<uint32_t>(file_path.size());
    unsigned char length_bytes[4];
    encode_length(path_length, length_bytes);

    if (!send_all(fd, length_bytes, 4, communication_deadline) ||
        !send_all(fd, file_path.data(), file_path.size(),
                  communication_deadline)) {
        std::cerr << "Communication with application failed.\n";
        close(fd);
        return 2;
    }

    unsigned char status = 0;
    unsigned char reason_length_bytes[4];

    if (!receive_all(fd, &status, 1, communication_deadline) ||
        !receive_all(fd, reason_length_bytes, 4,
                     communication_deadline)) {
        std::cerr << "Application response timed out or failed.\n";
        close(fd);
        return 2;
    }

    uint32_t reason_length = decode_length(reason_length_bytes);

    if (reason_length > 1024 * 1024 || status > 1) {
        std::cerr << "Invalid response from application.\n";
        close(fd);
        return 2;
    }

    std::string reason(reason_length, '\0');

    if (reason_length > 0 &&
        !receive_all(fd, reason.data(), reason_length,
                     communication_deadline)) {
        std::cerr << "Could not receive application response.\n";
        close(fd);
        return 2;
    }

    close(fd);

    if (status == 0) {
        std::cout << "Application accepted file: "
                  << file_path << '\n';
        return 0;
    }

    std::cerr << (reason.empty() ? "Application rejected file." : reason)
              << '\n';
    return 1;
}

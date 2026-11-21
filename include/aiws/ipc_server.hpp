#ifndef AIWS_IPC_SERVER_HPP
#define AIWS_IPC_SERVER_HPP

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace aiws::gui {
class MainWindow;
}

namespace aiws {

class IpcServer {
public:
    IpcServer(const std::string& socket_path,
              aiws::gui::MainWindow* window);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start();
    void stop();

private:
    void run();

    std::string socket_path_;
    aiws::gui::MainWindow* window_;

    std::atomic<bool> stopping_{false};
    std::thread worker_;
    int server_fd_{-1};

    std::mutex client_mutex_;
    int active_client_fd_{-1};
};

} // namespace aiws

#endif

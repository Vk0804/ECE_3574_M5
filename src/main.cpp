
#include "aiws/gui/main_window.hpp"
#include "aiws/ipc_server.hpp"

#include <QApplication>
#include <QMessageBox>

#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>


int main(int argc, char* argv[]) {
   
    std::string socket_path;
    int build_delay_ms = 0;

    // Default socket path: /tmp/aiws-$USER.sock
    const char* username = std::getenv("USER");

    if (username != nullptr && username[0] != '\0') {
        socket_path = "/tmp/aiws-" + std::string(username) + ".sock";
    } else {
        socket_path = "/tmp/aiws-" + std::to_string(getuid()) + ".sock";
    }

    // Parse application arguments.
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];

        if (argument == "--ipc-socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (argument == "--build-delay-ms" && i + 1 < argc) {
            try {
                build_delay_ms = std::stoi(argv[++i]);

                if (build_delay_ms < 0) {
                    std::cerr << "Build delay cannot be negative.\n";
                    return 2;
                }
            } catch (...) {
                std::cerr << "Invalid build delay.\n";
                return 2;
            }
        } else {
            std::cerr << "Unknown or incomplete argument: "
                      << argument << '\n';
            return 2;
        }
    }

    // Create the Qt application.
    int qt_argc = 1;
    char* qt_argv[] = {argv[0], nullptr};
    QApplication app(qt_argc, qt_argv);

    // Construct and configure the MainWindow.
    aiws::gui::MainWindow window;
    window.set_build_delay_ms(build_delay_ms);

    // Start the remote-add server.
    aiws::IpcServer server(socket_path, &window);

    if (!server.start()) {
        std::cerr << "Could not start IPC server at: "
                  << socket_path << '\n';
        return 1;
    }

    std::cout << "IPC server listening at: "
              << socket_path << '\n';

    // Register the ExitCallback.
    window.set_exit_callback([&server, &window](QWidget* parent) {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(
                parent,
                "Exit AI Workspace",
                "Are you sure you want to exit?",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);

        if (answer == QMessageBox::Yes) {
            server.stop();
            window.request_exit_after_build();
        }
    });

    // Show the window and run the event loop.
    window.show();

    return app.exec();
}

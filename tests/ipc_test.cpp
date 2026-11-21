
#include "aiws/ipc_server.hpp"
#include "aiws/gui/main_window.hpp"

#include <QtTest/QtTest>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

class IpcTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir temp_dir_;
    std::unique_ptr<aiws::gui::MainWindow> window_;
    std::unique_ptr<aiws::IpcServer> server_;
    QString socket_path_;

    static bool sendAll(int fd, const char* data, std::size_t size) {
        std::size_t sent = 0;

        while (sent < size) {
            ssize_t result = send(
                fd, data + sent, size - sent, MSG_NOSIGNAL);

            if (result <= 0) {
                return false;
            }

            sent += static_cast<std::size_t>(result);
        }

        return true;
    }

    static bool receiveAll(int fd, char* data, std::size_t size) {
        std::size_t received = 0;

        while (received < size) {
            ssize_t result = recv(
                fd, data + received, size - received, 0);

            if (result <= 0) {
                return false;
            }

            received += static_cast<std::size_t>(result);
        }

        return true;
    }

    // Sends a request using the same protocol as aiws_send.
    int sendRequest(const QString& path, bool fragmented = false) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        sockaddr_un address{};
        address.sun_family = AF_UNIX;

        QByteArray socket_bytes = socket_path_.toUtf8();

        if (socket_bytes.size() >=
            static_cast<int>(sizeof(address.sun_path))) {
            close(fd);
            return -1;
        }

        std::strncpy(
            address.sun_path,
            socket_bytes.constData(),
            sizeof(address.sun_path) - 1);

        if (::connect(
                fd,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) < 0) {
            close(fd);
            return -1;
        }

        QByteArray path_bytes = path.toUtf8();

        uint32_t length = htonl(
            static_cast<uint32_t>(path_bytes.size()));

        std::vector<char> request(
            sizeof(length) + path_bytes.size());

        std::memcpy(request.data(), &length, sizeof(length));

        std::memcpy(
            request.data() + sizeof(length),
            path_bytes.constData(),
            path_bytes.size());

        bool sent = false;

        if (fragmented) {
            // Deliberately split the request into multiple writes.
            sent = sendAll(fd, request.data(), 2);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));

            sent = sent && sendAll(
                fd, request.data() + 2, 2);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));

            sent = sent && sendAll(
                fd,
                request.data() + 4,
                request.size() - 4);
        } else {
            sent = sendAll(
                fd, request.data(), request.size());
        }

        if (!sent) {
            close(fd);
            return -1;
        }

        // Read the 5-byte response header.
        char response[5]{};

        if (!receiveAll(fd, response, sizeof(response))) {
            close(fd);
            return -1;
        }

        uint32_t reason_length;
        std::memcpy(
            &reason_length, response + 1, sizeof(reason_length));

        reason_length = ntohl(reason_length);

        std::vector<char> reason(reason_length);

        if (reason_length > 0 &&
            !receiveAll(fd, reason.data(), reason_length)) {
            close(fd);
            return -1;
        }

        int status = static_cast<unsigned char>(response[0]);

        close(fd);
        return status;
    }

    QString currentStatus() const {
        const auto labels = window_->findChildren<QLabel*>();

        for (QLabel* label : labels) {
            if (label->text().startsWith("Current status:")) {
                return label->text();
            }
        }

        return {};
    }

    bool createFile(const QString& name) {
        QFile file(temp_dir_.filePath(name));

        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }

        file.write("CR1 IPC test document");
        file.close();

        return true;
    }

private slots:

    void init() {
        QVERIFY(temp_dir_.isValid());

        window_ = std::make_unique<aiws::gui::MainWindow>();

        socket_path_ = temp_dir_.filePath("test.sock");

        server_ = std::make_unique<aiws::IpcServer>(
            socket_path_.toStdString(),
            window_.get());

        QVERIFY(server_->start());

        window_->show();
    }

    void cleanup() {
        if (server_) {
            server_->stop();
            server_.reset();
        }

        window_.reset();
    }

    // Test 1: Server creates its socket.
    void serverStarts() {
        QVERIFY(QFileInfo::exists(socket_path_));
    }

    // Test 2: Valid document is remotely added.
    void validDocumentRequest() {
        QVERIFY(createFile("valid.txt"));

        int status = sendRequest(temp_dir_.filePath("valid.txt"));

        QCOMPARE(status, 0);

        QTRY_VERIFY(
            currentStatus().contains("Added valid.txt"));
    }

    // Test 3: Missing document is rejected.
    void missingDocumentRejected() {
        int status = sendRequest(
            temp_dir_.filePath("missing.txt"));

        QCOMPARE(status, 1);
    }

    // Test 4: Filename containing spaces.
    void filenameWithSpaces() {
        QVERIFY(createFile("my test file.txt"));

        int status = sendRequest(
            temp_dir_.filePath("my test file.txt"));

        QCOMPARE(status, 0);

        QTRY_VERIFY(
            currentStatus().contains("my test file.txt"));
    }

    // Test 5: UTF-8 filename.
    void utf8Filename() {
        QVERIFY(createFile(QString::fromUtf8("café résumé.txt")));

        int status = sendRequest(
            temp_dir_.filePath(QString::fromUtf8("café résumé.txt")));

        QCOMPARE(status, 0);

        QTRY_VERIFY(
            currentStatus().contains(QString::fromUtf8("café résumé.txt")));
    }

    // Test 6: Filename containing a newline.
    void newlineFilename() {
        QString name = QStringLiteral("first\nsecond.txt");

        QVERIFY(createFile(name));

        int status = sendRequest(temp_dir_.filePath(name));

        QCOMPARE(status, 0);
    }

    // Test 7: Request sent in multiple writes.
    void fragmentedRequest() {
        QVERIFY(createFile("fragment.txt"));

        int status = sendRequest(
            temp_dir_.filePath("fragment.txt"), true);

        QCOMPARE(status, 0);

        QTRY_VERIFY(
            currentStatus().contains("Added fragment.txt"));
    }

    // Test 8: Build Corpus button works.
    void buildCorpusButton() {
        QVERIFY(createFile("build.txt"));

        QVERIFY(window_->add_document_file(
            temp_dir_.filePath("build.txt")));

        window_->set_build_delay_ms(100);

        QPushButton* build_button = nullptr;

        for (QPushButton* button :
             window_->findChildren<QPushButton*>()) {
            if (button->text().contains(
                    "Build", Qt::CaseInsensitive)) {
                build_button = button;
                break;
            }
        }

        QVERIFY(build_button != nullptr);

        QTest::mouseClick(build_button, Qt::LeftButton);

        QTRY_VERIFY_WITH_TIMEOUT(
            currentStatus().contains("Corpus built successfully"),
            3000);
    }

    // Test 9: Exit button invokes the registered callback.
    void exitButtonCallsCallback() {
        bool callback_called = false;

        window_->set_exit_callback(
            [&callback_called](QWidget*) {
                callback_called = true;
            });

        QPushButton* exit_button =
            window_->findChild<QPushButton*>("");

        for (QPushButton* button :
             window_->findChildren<QPushButton*>()) {
            if (button->text() == "Exit") {
                exit_button = button;
                break;
            }
        }

        QVERIFY(exit_button != nullptr);

        QTest::mouseClick(exit_button, Qt::LeftButton);

        QVERIFY(callback_called);
    }

    // Test 10: Server removes its socket when stopped.
    void serverStops() {
        QVERIFY(QFileInfo::exists(socket_path_));

        server_->stop();

        QVERIFY(!QFileInfo::exists(socket_path_));
    }
};

QTEST_MAIN(IpcTests)

#include "ipc_test.moc"

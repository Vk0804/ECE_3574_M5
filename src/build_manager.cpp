#include "aiws/build_manager.hpp"

#include <QMetaObject>

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace aiws {

struct BuildManager::Completion {
    bool success{false};
    QString error;
    std::unique_ptr<ProcessingCore> result;
    std::size_t workspace_version{0};
};

BuildManager::BuildManager(QObject* parent)
    : QObject(parent) {}

BuildManager::~BuildManager() {
    if (worker_.joinable()) {
        worker_.join();
    }
}
//join - wats for the worker to finish
//detach - the worker runs independelty wothout the
//owner managining itc completeion

// starts a background build
//workspace_ - documents to process 
//build_delay_ms - how much additional delay to simulate per document
//workspace_version_ - the version of the workspace being built
bool BuildManager::start_build(Workspace workspace,
                               int delay_ms,
                               std::size_t workspace_version) {
    if (building_) {
        return false;
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    result_.reset();
    building_ = true;

    // thread worker
    try {
        worker_ = std::thread(
            [this,
             workspace = std::move(workspace),
             delay_ms = std::max(0, delay_ms),
             workspace_version]() mutable {
// want worker to process its own workspacce data rather 
// than depend on the GUI changingthe original workspace

                auto completion = std::make_shared<Completion>();
                completion->workspace_version = workspace_version;

                try {
                    // Add the requested delay without blocking the GUI.
                    for (std::size_t i = 0;
                         i < workspace.documents().size();
                         ++i) {
                        if (delay_ms > 0) {
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(delay_ms));
                        }
                    }

                    // The worker owns this ProcessingCore exclusively.
                    auto core = std::make_unique<ProcessingCore>();
                    core->rebuild(workspace);

                    completion->result = std::move(core);
                    completion->success = true;

                    // handle errors 
                } catch (const std::exception& error) {
                    completion->error =
                        QString::fromUtf8(error.what());
                } catch (...) {
                    completion->error =
                        QStringLiteral("Unknown error while building corpus.");
                }

                // Deliver the completion to the GUI thread.
                QMetaObject::invokeMethod(
                    this,
                    [this, completion]() {
                        handle_completion(completion);
                    },
                    Qt::QueuedConnection);
            });
    } catch (const std::exception&) {
        building_ = false;
        return false;
    }

    return true;
}

// Reports whether a build is running
bool BuildManager::is_building() const noexcept {
    return building_;
}

// Transfers the completed corpus to the caller
std::unique_ptr<ProcessingCore> BuildManager::take_result() {
    return std::move(result_);
}

// Reports which workspace version was built
std::size_t BuildManager::completed_version() const noexcept {
    return result_version_;
}

//Processes the worker's completion notification
void BuildManager::handle_completion(
    std::shared_ptr<Completion> completion) {

    // The queued notification means the worker has finished posting
    // its result. Join it before allowing another build to start.
    if (worker_.joinable()) {
        worker_.join();
    }

    result_ = std::move(completion->result);
    result_version_ = completion->workspace_version;
    building_ = false;

    emit build_finished(completion->success, completion->error);
}

}  // namespace aiws
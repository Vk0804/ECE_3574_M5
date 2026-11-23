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
    std::size_t generation{0};
};

BuildManager::BuildManager(QObject* parent)
    : QObject(parent), 
    worker_(&BuildManager::worker_loop, this) {}

BuildManager::~BuildManager() {
    shutdown();
    
}
/*if (worker_.joinable()) {
        worker_.join();
    }*/
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

    // Do not accept another build while one is running.
    if (building_) {
        return false;
    }

    result_.reset();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopping_) {
            return false;
        }

        // A new generation makes any previous request outdated.
        ++generation_;

        // Store the workspace for the worker to process.
        pending_request_ = Request{
            std::move(workspace),
            std::max(0, delay_ms),
            workspace_version,
            generation_
        };

        building_ = true;
    }

    // Wake the worker so it can process the request.
    condition_.notify_one();

    return true;
}

// Cancels the current build or any pending request.
void BuildManager::cancel_build() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        ++generation_;
        pending_request_.reset();
        building_ = false;
    }

    result_.reset();

    // Wake the worker if it is waiting or simulating a delay.
    condition_.notify_one();
}

// Stops the persistent worker and waits for it to finish.
void BuildManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        stopping_ = true;
        ++generation_;
        pending_request_.reset();
        building_ = false;
    }

    condition_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
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

// Checks whether a build request has been cancelled or superseded.
bool BuildManager::request_cancelled(
    std::size_t generation) const {

    std::lock_guard<std::mutex> lock(mutex_);

#ifdef AIWS_FAULT_NO_CANCEL
    (void)generation;
    return stopping_;
#else
    return stopping_ || generation_ != generation;
#endif
}

void BuildManager::worker_loop() {
    while (true) {
        Request request;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            condition_.wait(lock, [this]() {
            #ifdef AIWS_FAULT_SILENT_SHUTDOWN
                return pending_request_.has_value();
            #else
                return stopping_ || pending_request_.has_value();
            #endif
            });

            if (stopping_) {
                return;
            }

            request = std::move(*pending_request_);
            pending_request_.reset();
        }

        auto completion = std::make_shared<Completion>();
        completion->workspace_version = request.workspace_version;
        completion->generation = request.generation;

        try {
            for (std::size_t i = 0;
                 i < request.workspace.documents().size();
                 ++i) {

                if (request.delay_ms > 0) {
                    std::unique_lock<std::mutex> lock(mutex_);

                const bool cancelled = condition_.wait_for(
                    lock,
                    std::chrono::milliseconds(request.delay_ms),
                    [this, &request]() {
                #ifdef AIWS_FAULT_NO_CANCEL
                        return stopping_;
                #else
                        return stopping_ ||
                         generation_ != request.generation;
                #endif
                    });

                    if (cancelled) {
                        break;
                    }
                }

                if (request_cancelled(request.generation)) {
                    break;
                }
            }

            if (!request_cancelled(request.generation)) {
                auto core = std::make_unique<ProcessingCore>();
                core->rebuild(request.workspace);

                if (!request_cancelled(request.generation)) {
                    completion->result = std::move(core);
                    completion->success = true;
                }
            }

        } catch (const std::exception& error) {
            completion->error = QString::fromUtf8(error.what());
        } catch (...) {
            completion->error =
                QStringLiteral("Unknown error while building corpus.");
        }

        if (request_cancelled(request.generation)) {
            continue;
        }

        QMetaObject::invokeMethod(
            this,
            [this, completion]() {
                handle_completion(completion);
            },
            Qt::QueuedConnection);
    }
}

//Processes the worker's completion notification
void BuildManager::handle_completion(
    std::shared_ptr<Completion> completion) {

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopping_ ||
            generation_ != completion->generation) {
            return;
        }
    }

    result_ = std::move(completion->result);
    result_version_ = completion->workspace_version;
    building_ = false;

    emit build_finished(completion->success, completion->error);
}

}  // namespace aiws

#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <QObject>
#include <QString>

#include <cstddef>
#include <memory>
#include <thread>

namespace aiws {

class BuildManager final : public QObject {
    Q_OBJECT

public:
    explicit BuildManager(QObject* parent = nullptr);
    ~BuildManager() override;

    BuildManager(const BuildManager&) = delete;
    BuildManager& operator=(const BuildManager&) = delete;

    bool start_build(Workspace workspace,
                     int delay_ms,
                     std::size_t workspace_version);

    bool is_building() const noexcept;

    std::unique_ptr<ProcessingCore> take_result();

    std::size_t completed_version() const noexcept;

signals:
    void build_finished(bool success, const QString& error);

private:
    struct Completion;

    void handle_completion(std::shared_ptr<Completion> completion);

    std::thread worker_;
    bool building_{false};

    std::unique_ptr<ProcessingCore> result_;
    std::size_t result_version_{0};
};

}  // namespace aiws

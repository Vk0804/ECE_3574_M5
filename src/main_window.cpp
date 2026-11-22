#include "aiws/gui/main_window.hpp"

#include "aiws/build_manager.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>


#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>
#include <vector>
#include <QApplication>
#include <memory>


namespace aiws::gui {

     namespace {
void show_status(QLabel* label, const QString& message) {
    label->setText("Current status: " + message);
}

} 

  class MainWindow::State {
public:
    virtual ~State() = default;

    virtual void workspace_changed(MainWindow& window) = 0;
    virtual void build_requested(MainWindow& window) = 0;
    virtual void reset_requested(MainWindow& window) = 0;
    virtual void exit_requested(MainWindow& window) = 0;

    virtual void build_finished(
        MainWindow& window,
        bool success,
        const QString& error) = 0;

    virtual bool is_building() const noexcept = 0;
    virtual void enter(MainWindow&) {}
};

class MainWindow::ReadyState final : public MainWindow::State {
public:
    void workspace_changed(MainWindow& window) override;
    void build_requested(MainWindow& window) override;
    void reset_requested(MainWindow& window) override;
    void exit_requested(MainWindow& window) override;

    void build_finished(
        MainWindow& window,
        bool success,
        const QString& error) override;

    bool is_building() const noexcept override;
};

class MainWindow::BuildingState final : public MainWindow::State {
public:
    void workspace_changed(MainWindow& window) override;
    void build_requested(MainWindow& window) override;
    void reset_requested(MainWindow& window) override;
    void exit_requested(MainWindow& window) override;

    void build_finished(
        MainWindow& window,
        bool success,
        const QString& error) override;

    bool is_building() const noexcept override;
};

class MainWindow::ExitingState final : public MainWindow::State {
public:
    void workspace_changed(MainWindow& window) override;
    void build_requested(MainWindow& window) override;
    void reset_requested(MainWindow& window) override;
    void exit_requested(MainWindow& window) override;
    void enter(MainWindow& window) override;

    void build_finished(
        MainWindow& window,
        bool success,
        const QString& error) override;

    bool is_building() const noexcept override;
};

// ReadyState

void MainWindow::ReadyState::workspace_changed(MainWindow& window) {
    window.invalidate_corpus();
}

void MainWindow::ReadyState::build_requested(MainWindow& window) {
    if (window.workspace_.documents().empty()) {
        show_status(
            window.status_label_,
            "Add documents before building the corpus.");
        return;
    }

    window.clear_processing_outputs();
    window.corpus_current_ = false;

    window.workspace_widget_->set_corpus_status("Corpus: Building...");
    show_status(window.status_label_, "Building corpus in background...");

    const bool started = window.build_manager_.start_build(
        window.workspace_,
        window.build_delay_ms_,
        window.workspace_version_);

    if (!started) {
        show_status(window.status_label_, "Could not start corpus build.");
        window.workspace_widget_->set_corpus_status("Corpus: Not built");
        return;
    }

    window.transition_to(std::make_unique<BuildingState>());
}

void MainWindow::ReadyState::reset_requested(MainWindow& window) {
    window.reset_workspace();
}

void MainWindow::ReadyState::exit_requested(MainWindow& window) {
    window.transition_to(std::make_unique<ExitingState>());
}

void MainWindow::ReadyState::build_finished(
    MainWindow& window,
    bool success,
    const QString& error) {
    (void)window;
    (void)success;
    (void)error;
}

bool MainWindow::ReadyState::is_building() const noexcept {
    return false;
}


// BuildingState

void MainWindow::BuildingState::workspace_changed(MainWindow& window) {
    window.build_manager_.cancel_build();
    window.invalidate_corpus();

    show_status(window.status_label_, "Workspace changed. Build the corpus again.");
    window.workspace_widget_->set_corpus_status("Corpus: Out of date");

    window.transition_to(std::make_unique<ReadyState>());
}

void MainWindow::BuildingState::build_requested(MainWindow& window) {
    show_status(window.status_label_, "A corpus build is already running.");
}

void MainWindow::BuildingState::reset_requested(MainWindow& window) {
    window.build_manager_.cancel_build();
    window.reset_workspace();

    show_status(window.status_label_, "Workspace changed. Build the corpus again.");
    window.workspace_widget_->set_corpus_status("Corpus: Out of date");

    window.transition_to(std::make_unique<ReadyState>());
}

void MainWindow::BuildingState::exit_requested(MainWindow& window) {
    window.transition_to(std::make_unique<ExitingState>());
}

void MainWindow::BuildingState::build_finished(
    MainWindow& window,
    bool success,
    const QString& error) {

    if (!success) {
        show_status(window.status_label_, "Corpus build failed: " + error);
        window.workspace_widget_->set_corpus_status("Corpus: Build failed");
        window.corpus_current_ = false;
        window.transition_to(std::make_unique<ReadyState>());
        return;
    }

    // Reject a result if the workspace changed while it was building.
    if (window.build_manager_.completed_version() !=
        window.workspace_version_) {

        (void)window.build_manager_.take_result();

        show_status(
            window.status_label_,
            "Workspace changed. Build the corpus again.");
        window.workspace_widget_->set_corpus_status("Corpus: Out of date");
        window.corpus_current_ = false;
        window.transition_to(std::make_unique<ReadyState>());
        return;
    }

    auto completed_core = window.build_manager_.take_result();

    if (!completed_core) {
        show_status(
            window.status_label_,
            "Corpus build returned no result.");
        window.corpus_current_ = false;
        window.transition_to(std::make_unique<ReadyState>());
        return;
    }

    window.processing_core_ = std::move(*completed_core);
    window.corpus_current_ = true;

    window.clear_processing_outputs();

    window.workspace_widget_->set_corpus_status(
        QString("Corpus: %1 chunks")
            .arg(window.processing_core_.chunk_count()));

    show_status(window.status_label_, "Corpus built successfully.");
    window.transition_to(std::make_unique<ReadyState>());
}

bool MainWindow::BuildingState::is_building() const noexcept {
    return true;
}


// ExitingState

void MainWindow::ExitingState::enter(MainWindow& window) {
    window.build_manager_.shutdown();
    QApplication::quit();
}

void MainWindow::ExitingState::workspace_changed(MainWindow& window) {
    (void)window;
}

void MainWindow::ExitingState::build_requested(MainWindow& window) {
    (void)window;
}

void MainWindow::ExitingState::reset_requested(MainWindow& window) {
    (void)window;
}

void MainWindow::ExitingState::exit_requested(MainWindow& window) {
    (void)window;
}

void MainWindow::ExitingState::build_finished(
    MainWindow& window,
    bool success,
    const QString& error) {
    (void)window;
    (void)success;
    (void)error;
}

bool MainWindow::ExitingState::is_building() const noexcept {
    return false;
}

MainWindow::~MainWindow() {
    build_manager_.shutdown();
    state_.reset();
}

void MainWindow::transition_to(std::unique_ptr<State> next_state) {
    state_ = std::move(next_state);
    state_->enter(*this);
    update_action_states();
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      build_delay_ms_(0),
      state_(std::make_unique<ReadyState>()) {
    
    setWindowTitle("AI Workspace");
    resize(900, 900);

    workspace_widget_ = new WorkspaceWidget(this);
    search_widget_ = new SearchWidget(this);
    term_inspector_widget_ = new TermInspectorWidget(this);
    context_widget_ = new ContextWidget(this);

    // Header, Reset and Exit need a non-white background.
    auto* header = new QLabel("AI Workspace", this);
    header->setStyleSheet(
        "background-color: #6b1f2b; color: white; font-size: 22px; font-weight: bold; padding: 12px;");


    status_label_ = new QLabel(this);
    show_status(status_label_, "Ready");
    reset_button_ = new QPushButton("Reset", this);
    reset_button_->setStyleSheet("background-color: #d9a441; color: black; padding: 6px 24px;");
    exit_button_ = new QPushButton("Exit", this);
    exit_button_->setStyleSheet("background-color: #3f6fb5; color: white; padding: 6px 24px;");

    // widgets and layouts 
    auto* bottom_row = new QHBoxLayout();
    bottom_row->addWidget(term_inspector_widget_, 1);
    bottom_row->addWidget(context_widget_, 1);

    auto* footer_row = new QHBoxLayout();
    footer_row->addWidget(status_label_, 1);
    footer_row->addWidget(reset_button_);
    footer_row->addWidget(exit_button_);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(header);
    layout->addWidget(workspace_widget_, 3);
    layout->addWidget(search_widget_, 4);
    layout->addLayout(bottom_row, 2);
    layout->addLayout(footer_row);
    setCentralWidget(central);


    // Slots and signals 
    connect(workspace_widget_, &WorkspaceWidget::add_document_requested,
            this, &MainWindow::add_document);
    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested,
            this, &MainWindow::build_corpus);
    connect(workspace_widget_, &WorkspaceWidget::document_selected,
            this, &MainWindow::document_selected);
    connect(search_widget_, &SearchWidget::search_requested,
            this, &MainWindow::search_requested);
    connect(search_widget_, &SearchWidget::query_changed,
            this, &MainWindow::query_changed);
    connect(search_widget_, &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested);
            // direct values
    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);
    connect(term_inspector_widget_, &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);
    connect(context_widget_, &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);
    connect(reset_button_, &QPushButton::clicked, this, &MainWindow::reset_application);
    connect(exit_button_, &QPushButton::clicked, this, &MainWindow::exit_application);

    //M5 
    connect(&build_manager_, &BuildManager::build_finished,
        this, &MainWindow::on_build_finished);

    update_action_states();

}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

void MainWindow::request_exit_after_build() {
    state_->exit_requested(*this);
}

void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(this, "Add Document");

    if (path.isEmpty()) {
        return;
    }

    if (!add_document_file(path)) {
        show_status(status_label_, "Could not read selected document.");
    }
}

// M5 
bool MainWindow::add_document_file(const QString& path) {
    QFileInfo info(path);
    QFile file(info.absoluteFilePath());

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const std::string text = file.readAll().toStdString();

    workspace_.add_document(
        Document(info.absoluteFilePath().toStdString(),
                 info.fileName().toStdString(),
                 text));

    workspace_widget_->add_document(info.fileName());

++workspace_version_;

show_status(status_label_, "Added " + info.fileName());
state_->workspace_changed(*this);
    return true;
}

void MainWindow::build_corpus() {
    state_->build_requested(*this);
}

//includes name and text
void MainWindow::document_selected(int row) {
        const std::vector<Document>& documents = workspace_.documents();
    if (row < 0 || static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }
    const Document& document = documents[static_cast<std::size_t>(row)];
    workspace_widget_->show_selected_document(QString::fromStdString(document.title()),
                                              QString::fromStdString(document.text()));
}

//Search but clear context and term inspector because they are based on the same corpus state.
void MainWindow::search_requested(const QString& query, int k) {
    const std::vector<SearchResult> results = processing_core_.search(query.toStdString(), k);
    context_widget_->clear_context();
    term_inspector_widget_->clear_statistics();
    search_widget_->set_results(results);
    show_status(status_label_, "Search complete");
}

// Update the search button state when the query changes.
void MainWindow::query_changed(const QString& query) {
    (void)query;
    update_action_states(); // To suppress unused-parameter warning.
}

// Build context for the current query and search settings, and present it.
void MainWindow::build_context_requested(int token_budget) {
     context_widget_->set_context(processing_core_.build_context(
        search_widget_->query().toStdString(), search_widget_->result_count(),
        static_cast<std::size_t>(token_budget)));
    show_status(status_label_, "Context built");
}

//
void MainWindow::source_document_requested(const QString& document_id) {
    const std::vector<Document>& documents = workspace_.documents();
    for (std::size_t i = 0; i < documents.size(); ++i) {
        if (documents[i].id() == document_id.toStdString()) {
            workspace_widget_->select_document(static_cast<int>(i));
            return;
        }
    }
}

void MainWindow::term_inspection_requested(const QString& term,
                                           const QString& chunk_id) {
    const std::string text = term.toStdString();
    const bool has_chunk = !chunk_id.isEmpty();
    term_inspector_widget_->set_statistics(
        processing_core_.document_frequency(text), has_chunk,
        has_chunk ? processing_core_.term_frequency(text, chunk_id.toStdString()) : 0);
}

//Clear from all wdigets 
void MainWindow::reset_application() {
    state_->reset_requested(*this);
}

void MainWindow::reset_workspace() {
    workspace_.clear();
    ++workspace_version_;

    corpus_current_ = false;

    const std::array<Resettable*, 4> parts{
        workspace_widget_,
        search_widget_,
        term_inspector_widget_,
        context_widget_
    };

    for (Resettable* part : parts) {
        part->reset();
    }

    clear_processing_outputs();

    workspace_widget_->set_corpus_status("Corpus: Not built");
    show_status(status_label_, "Application reset.");

    update_action_states();
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

//enabling buttons altogether based on corpus state
void MainWindow::update_action_states() {
    const bool has_query = search_widget_->has_query();
    const bool is_building = state_->is_building();

    workspace_widget_->set_build_corpus_enabled(
        !workspace_.documents().empty() && !is_building);

    search_widget_->set_search_enabled(corpus_current_ && has_query);
    context_widget_->set_build_context_enabled(corpus_current_ && has_query);
    term_inspector_widget_->set_inspection_enabled(corpus_current_);
}

//Both cannot drift apart because they are both based on the same corpus state.
void MainWindow::invalidate_corpus() {
    corpus_current_ = false;
    clear_processing_outputs();
    workspace_widget_->set_corpus_status("Corpus: Not built");
    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

// M5 Implementation 
void MainWindow::on_build_finished(bool success, const QString& error) {
    state_->build_finished(*this, success, error);
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    build_delay_ms_ = std::max(0, ms_per_document);
}

//BuildManager build_manager_;

}  // namespace aiws::gui

#include "aiws/gui/search_widget.hpp"


#include "aiws/gui/search_result_model.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace aiws::gui {

SearchWidget::SearchWidget(QWidget* parent) : QWidget(parent) {

    query_edit_ = new QLineEdit(this);
    query_edit_->setPlaceholderText("Search the current corpus");

    result_count_spin_ = new QSpinBox(this);
    result_count_spin_->setRange(1, 100);
    result_count_spin_->setValue(3);

    search_button_ = new QPushButton("Search", this);
    search_button_->setEnabled(false);

    result_model_ = new SearchResultModel(this);
    results_view_ = new QTableView(this);
    results_view_->setModel(result_model_);  // must come before selectionModel()
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_view_->horizontalHeader()->setStretchLastSection(true);

    view_source_button_ = new QPushButton("View Source", this);
    view_source_button_->setEnabled(false);

    selected_result_text_ = new QPlainTextEdit(this);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText("Select a result to inspect its details.");

    auto* query_row = new QHBoxLayout();
    query_row->addWidget(new QLabel("Query:", this));
    query_row->addWidget(query_edit_, 1);
    query_row->addWidget(new QLabel("Results:", this));
    query_row->addWidget(result_count_spin_);
    query_row->addWidget(search_button_);

    auto* selected_row = new QHBoxLayout();
    selected_row->addWidget(new QLabel("Selected Result:", this));
    selected_row->addStretch(1);
    selected_row->addWidget(view_source_button_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(query_row);
    layout->addWidget(results_view_, 3);
    layout->addLayout(selected_row);
    layout->addWidget(selected_result_text_, 2);

    //Connect signals to slots
    connect(query_edit_, &QLineEdit::textChanged, this, &SearchWidget::query_changed);
    //
    connect(search_button_, &QPushButton::clicked, this, &SearchWidget::request_search);
    connect(view_source_button_, &QPushButton::clicked,
            this, &SearchWidget::request_source_document);
    connect(results_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SearchWidget::show_selected_result);
}


QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query_edit_->text().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);

}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    clear_results();
    result_model_->set_results(std::move(results));
}

//clears the results and resets the view
void SearchWidget::clear_results() {
    result_model_->clear();
    results_view_->selectionModel()->clearSelection();
    selected_result_text_->clear();
    view_source_button_->setEnabled(false);
    emit selected_chunk_changed(QString());
}

void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(3);
    clear_results();
    search_button_->setEnabled(false);
}

//emiting a signal to main 
void SearchWidget::request_search() {
    emit search_requested(query_edit_->text(), result_count_spin_->value());
}

// emit to Main 
void SearchWidget::show_selected_result() {
     const QModelIndexList rows =
        results_view_->selectionModel()->selectedRows();

    const SearchResult* result =
        rows.isEmpty()
            ? nullptr
            : result_model_->result_at(rows.first().row());

    if (result == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    selected_result_text_->setPlainText(
        QString("Document: %1\nChunk: %2\nScore: %3\n\n%4")
            .arg(QString::fromStdString(result->document_id))
            .arg(static_cast<qulonglong>(result->chunk_sequence))
            .arg(result->score, 0, 'f', 6)
            .arg(QString::fromStdString(result->text)));

    view_source_button_->setEnabled(true);
    emit selected_chunk_changed(
        QString::fromStdString(result->chunk_id));
}

//emit to main - Document ID
void SearchWidget::request_source_document() {
    const QModelIndexList rows = results_view_->selectionModel()->selectedRows();
    const SearchResult* result =
        rows.isEmpty() ? nullptr : result_model_->result_at(rows.first().row());
    if (result != nullptr) {
        emit source_document_requested(QString::fromStdString(result->document_id));
    }
}

}  // namespace aiws::gui

#include "aiws/gui/term_inspector_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

TermInspectorWidget::TermInspectorWidget(QWidget* parent) : QWidget(parent) {
   

     term_edit_ = new QLineEdit(this);
    term_edit_->setPlaceholderText("Enter a term");

    inspect_button_ = new QPushButton("Inspect", this);
    inspect_button_->setEnabled(false);

    document_frequency_value_ = new QLabel("-", this);
    chunk_frequency_value_ = new QLabel("-", this);

    auto* term_row = new QHBoxLayout();
    term_row->addWidget(new QLabel("Term:", this));
    term_row->addWidget(term_edit_, 1);
    term_row->addWidget(inspect_button_);

    auto* stats_row = new QHBoxLayout();
    stats_row->addWidget(new QLabel("Document Frequency:", this));
    stats_row->addWidget(document_frequency_value_);
    stats_row->addWidget(new QLabel("Selected Chunk TF:", this));
    stats_row->addWidget(chunk_frequency_value_);
    stats_row->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Term Inspector:", this));
    layout->addLayout(term_row);
    layout->addLayout(stats_row);
    layout->addStretch(1);

    connect(inspect_button_, &QPushButton::clicked,
            this, &TermInspectorWidget::request_inspection);
}

//Enble used in update action_states
void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}


void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    selected_chunk_id_ = chunk_id;
}

void TermInspectorWidget::set_statistics(std::size_t document_frequency,
                                         bool has_chunk_frequency,
                                         std::size_t chunk_frequency) {
    document_frequency_value_->setText(QString::number(document_frequency));
    chunk_frequency_value_->setText(has_chunk_frequency ? QString::number(chunk_frequency)
                                                        : QString("-"));
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText("-");
    chunk_frequency_value_->setText("-");
}

void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    inspect_button_->setEnabled(false);
}

void TermInspectorWidget::request_inspection() {
    const QString term = term_edit_->text().trimmed();
    if (term.isEmpty()) {
        return;
    }
    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui

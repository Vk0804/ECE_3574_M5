#include "aiws/gui/workspace_widget.hpp"


#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>


namespace aiws::gui {

WorkspaceWidget::WorkspaceWidget(QWidget* parent) : QWidget(parent) {
    
add_document_button_ = new QPushButton("Add Document", this);
    build_corpus_button_ = new QPushButton("Build Corpus", this);
    build_corpus_button_->setEnabled(false);
    document_list_ = new QListWidget(this);
    corpus_status_label_ = new QLabel("Corpus: Not built", this);

    document_title_label_ = new QLabel("Selected Document -", this);
    document_text_ = new QPlainTextEdit(this);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText("Select a document to view its contents.");

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(new QLabel("Workspace", this));
    buttons->addWidget(add_document_button_);
    buttons->addWidget(build_corpus_button_);
    buttons->addStretch(1);

    auto* left = new QVBoxLayout();
    left->addLayout(buttons);
    left->addWidget(document_list_, 1);
    left->addWidget(corpus_status_label_);

    auto* right = new QVBoxLayout();
    right->addWidget(document_title_label_);
    right->addWidget(document_text_, 1);

    auto* layout = new QHBoxLayout(this);
    layout->addLayout(left, 1);
    layout->addLayout(right, 1);

    connect(add_document_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::add_document_requested);
    connect(build_corpus_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::build_corpus_requested);
    connect(document_list_, &QListWidget::currentRowChanged,
            this, &WorkspaceWidget::document_selected);
}

void WorkspaceWidget::add_document(const QString& title) {
    document_list_->addItem(title);
}

void WorkspaceWidget::select_document(int row) {
    document_list_->setCurrentRow(row);
}

void WorkspaceWidget::show_selected_document(const QString& title,
                                              const QString& text) {
    document_title_label_->setText("Selected Document - " + title);
    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_title_label_->setText("Selected Document -");
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) {
     corpus_status_label_->setText(text);
}

//enable courpus button - Update Action states
void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

void WorkspaceWidget::reset() {
    document_list_->clear();
    clear_selected_document();
    corpus_status_label_->setText("Corpus: Not built");
    build_corpus_button_->setEnabled(false);
}

}  // namespace aiws::gui

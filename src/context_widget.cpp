#include "aiws/gui/context_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

namespace aiws::gui {

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {
    context_budget_slider_ = new QSlider(Qt::Horizontal, this);
    context_budget_slider_->setRange(50, 1000);
    context_budget_slider_->setSingleStep(50);
    context_budget_slider_->setPageStep(50);
    context_budget_slider_->setValue(300);

    budget_value_label_ = new QLabel("300", this);

    build_context_button_ = new QPushButton("Build Context", this);
    build_context_button_->setEnabled(false);

    context_text_ = new QPlainTextEdit(this);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText("Built context will appear here.");

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel("Token budget:", this));
    controls->addWidget(context_budget_slider_, 1);
    controls->addWidget(budget_value_label_);
    controls->addWidget(build_context_button_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Context:", this));
    layout->addLayout(controls);
    layout->addWidget(context_text_, 1);

    connect(context_budget_slider_, &QSlider::valueChanged,
            this, &ContextWidget::budget_changed);
    connect(build_context_button_, &QPushButton::clicked,
            this, &ContextWidget::request_context);

}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
    
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
    
}

// Context is for each chunk for Id's and text
void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    QStringList blocks;
    for (const ContextItem& item : items) {
        QString block = 
        "Document:" + QString::fromStdString(item.document_id) + "\n" +
        "Chunk:" + QString::number(item.chunk_sequence) + "\n" +
        "Text:" + QString::fromStdString(item.text) + "\n" +
        "Tokens:" + QString::number(item.token_count) + "\n" +
        "Score:" + QString::number(item.score, 'f', 6) + "\n" +
        "Truncated:" + (item.truncated ? "Yes" : "No");
        blocks.append(block);
    }
    context_text_->setPlainText(blocks.join("\n\n"));

}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    context_budget_slider_->setValue(300);
    clear_context();
    build_context_button_->setEnabled(false);
}

void ContextWidget::request_context() {
    emit build_context_requested(context_budget_slider_->value());
}

void ContextWidget::budget_changed(int value) {
    
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui

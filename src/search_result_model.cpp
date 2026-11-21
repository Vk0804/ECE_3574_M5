#include "aiws/gui/search_result_model.hpp"

#include <QString>

#include <utility>


namespace aiws::gui {

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {
}
//no fixed row count 
int SearchResultModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(results_.size());
}

//fixed column count 
int SearchResultModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 5;
}

//Data in each column 
QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    const SearchResult* result = result_at(index.row());
    if (!index.isValid() || role != Qt::DisplayRole || result == nullptr) {
        return {};
    }
    switch (index.column()) {
        case 0: return index.row() + 1;
        case 1: return QString::fromStdString(result->document_id);
        case 2: return static_cast<qulonglong>(result->chunk_sequence);
        case 3: return QString::number(result->score, 'f', 6);
        case 4: return QString::fromStdString(result->text);
        default: return {};
    }
}

// Name of each column
QVariant SearchResultModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
        case 0: return QString("Rank");
        case 1: return QString("Document");
        case 2: return QString("Chunk");
        case 3: return QString("Score");
        case 4: return QString("Text");
        default: return {};
}
                                       }

void SearchResultModel::set_results(std::vector<SearchResult> results) {
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

//find each data in table
const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    if (row < 0 || static_cast<std::size_t>(row) >= results_.size()) {
        return nullptr;
    }
    return &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui

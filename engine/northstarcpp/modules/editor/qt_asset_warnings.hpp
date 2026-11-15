// First-class Qt asset diagnostic cards: warnings remain visible even
// when a drawable/material can render partially with missing dependencies.
#pragma once
#include <QWidget>
#include <QString>
#include <vector>

namespace northstarcpp::editor {
enum class AssetWarningKind { Model, Material, Texture, Prefab, Other };
struct AssetWarning {
    AssetWarningKind kind=AssetWarningKind::Other;
    QString reference;
    QString reason;
};
class AssetWarningPanel final : public QWidget {
public:
    explicit AssetWarningPanel(QWidget* parent=nullptr);
    void setWarnings(std::vector<AssetWarning> const& warnings);
    int warningCount() const {return warning_count_;}
private:
    QWidget* card_container_=nullptr;
    int warning_count_=0;
};
} // namespace northstarcpp::editor

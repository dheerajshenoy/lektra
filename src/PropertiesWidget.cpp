#include "PropertiesWidget.hpp"

PropertiesWidget::PropertiesWidget(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Document Properties");
    setModal(true);
    m_formLayout = new QFormLayout(this);
    setLayout(m_formLayout);
}

void
PropertiesWidget::setProperties(
    const std::vector<std::pair<QString, QString>> &properties) noexcept
{
    // Clear previous content
    QLayoutItem *child;
    while ((child = m_formLayout->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }

    // Add new labels
    for (const auto &[k, v] : properties)
    {
        QLabel *keyLabel   = new QLabel(k + ":", this);
        QLabel *valueLabel = new QLabel(v, this);
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        valueLabel->setWordWrap(true); // <-- wrap long text
        valueLabel->setSizePolicy(
            QSizePolicy::Expanding,
            QSizePolicy::Fixed); // allow horizontal growth
        m_formLayout->addRow(keyLabel, valueLabel);
    }

    adjustSize(); // optional: resize to fit new content
}

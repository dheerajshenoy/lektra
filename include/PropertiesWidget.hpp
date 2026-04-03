#pragma once

#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QString>
#include <QWidget>

class PropertiesWidget : public QDialog
{
    Q_OBJECT

public:
    explicit PropertiesWidget(QWidget *parent = nullptr);
    void setProperties(
        const std::vector<std::pair<QString, QString>> &properties) noexcept;

private:
    QFormLayout *m_formLayout;
};

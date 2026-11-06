#pragma once

#include <QButtonGroup>
#include <QColor>
#include <QDialog>
#include <QSpinBox>
#include <QVBoxLayout>

class ColorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ColorDialog(const std::vector<QColor> &colors,
                         const QColor &initial_color = QColor(),
                         QWidget *parent             = nullptr);

    const QColor &selectedColor() const
    {
        return m_selected_color;
    }

signals:
    void colorSelected(const QColor &color);

private:
    void initUI();

private:
    std::vector<QColor> m_colors;
    QColor m_selected_color;
    QColor m_initial_color;
    QButtonGroup *m_color_button_group = nullptr;
};

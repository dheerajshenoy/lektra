#include "ColorDialog.hpp"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>

ColorDialog::ColorDialog(const std::vector<QColor> &colors,
                         const QColor &initial_color, QWidget *parent)
    : QDialog(parent), m_colors(colors)
{
    setWindowTitle(tr("Select Color"));
    setModal(true);

    auto *mainLayout     = new QVBoxLayout(this);
    auto *colorGrid      = new QGridLayout();
    m_color_button_group = new QButtonGroup(this);
    m_color_button_group->setExclusive(true);

    const int maxCols = 5; // TODO: Make this configurable
    for (size_t i = 0; i < m_colors.size(); ++i)
    {
        const QColor &c = m_colors[i];
        auto *btn       = new QPushButton();
        btn->setFixedSize(30, 30); // Slightly larger for better UX
        btn->setCheckable(true);

        btn->setStyleSheet(
            QString(
                "QPushButton {"
                "  background-color: %1;"
                "  border: 3px solid transparent;" // Reserve the 3px space
                "  border-radius: 4px;"
                "}"
                "QPushButton:hover {"
                "  border-color: %2;" // Just change the color, size stays same
                "}"
                "QPushButton:checked {"
                "  border-color: #000;" // No "inset" needed, it will overlay
                                        // correctly
                "}")
                .arg(c.name())
                .arg(QPalette().highlight().color().name()));

        int row = static_cast<int>(i) / maxCols;
        int col = static_cast<int>(i) % maxCols;

        colorGrid->addWidget(btn, row, col);
        m_color_button_group->addButton(btn, static_cast<int>(i));
    }

    // Inside initUI, after the for loop:
    if (initial_color.isValid())
    {
        const QColor initial_rgb = initial_color.toRgb();
        auto channelDiff         = [](int a, int b)
        {
            return a > b ? a - b : b - a;
        };
        const int tolerance = 2;

        for (size_t i = 0; i < m_colors.size(); ++i)
        {
            const QColor color_rgb = m_colors[i].toRgb();
            if (channelDiff(color_rgb.red(), initial_rgb.red()) <= tolerance
                && channelDiff(color_rgb.green(), initial_rgb.green())
                       <= tolerance
                && channelDiff(color_rgb.blue(), initial_rgb.blue())
                       <= tolerance)
            {
                if (auto *btn
                    = m_color_button_group->button(static_cast<int>(i)))
                {
                    qDebug() << "Pre-selecting color: %s"
                             << initial_color.name().toStdString().c_str();
                    btn->setChecked(true);
                }
                break;
            }
        }
    }

    mainLayout->addLayout(colorGrid);

    // Standard Dialog Buttons
    auto *buttonBox = new QHBoxLayout();
    auto *okBtn     = new QPushButton(tr("OK"));
    auto *cancelBtn = new QPushButton(tr("Cancel"));

    okBtn->setDefault(true);
    buttonBox->addWidget(okBtn);
    buttonBox->addWidget(cancelBtn);

    mainLayout->addLayout(buttonBox);

    // Logic
    connect(okBtn, &QPushButton::clicked, this, [this]()
    {
        int id = m_color_button_group->checkedId();
        if (id != -1)
        {
            m_selected_color = m_colors[static_cast<size_t>(id)];
            accept();
        }
    });

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

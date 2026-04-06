#include "ColorDialog.hpp"

#include <QHBoxLayout>
#include <QPushButton>

ColorDialog::ColorDialog(QWidget *parent) : QDialog(parent)
{
    initUI();
}

void
ColorDialog::initUI()
{
    setWindowTitle(tr("Color Dialog"));
    setFixedSize(200, 150);
    setModal(true);

    m_layout                 = new QVBoxLayout(this);
    QGridLayout *colorLayout = new QGridLayout(this);

    static const QList<QColor> highlightColors = {
        {255, 255, 0, 128},   // Yellow
        {255, 165, 0, 128},   // Orange
        {255, 0, 0, 128},     // Red
        {0, 255, 0, 128},     // Green
        {0, 200, 255, 128},   // Cyan
        {128, 0, 255, 128},   // Purple
        {255, 105, 180, 128}, // Pink
        {0, 0, 255, 128},     // Blue
        {255, 255, 255, 128}, // White
    };

    int idx = 0;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            const QColor &c          = highlightColors[idx++];
            QPushButton *colorButton = new QPushButton(this);
            colorButton->setStyleSheet(QString(R"(
            QPushButton { background: rgb(%1, %2, %3); }
            QPushButton:focus { outline: 3px solid black; }
        )")
                                           .arg(c.red())
                                           .arg(c.green())
                                           .arg(c.blue()));
            connect(colorButton, &QPushButton::clicked, this,
                    [this, c]() { m_selected_color = c; });
            colorLayout->addWidget(colorButton, i, j);
        }
    }

    m_layout->addLayout(colorLayout);

    QPushButton *ok_btn     = new QPushButton(tr("OK"), this);
    QPushButton *cancel_btn = new QPushButton(tr("Cancel"), this);

    connect(ok_btn, &QPushButton::clicked, this, [this]()
    {
        emit colorSelected(m_selected_color);
        accept();
    });

    connect(cancel_btn, &QPushButton::clicked, this, [this]() { reject(); });

    m_layout->addWidget(ok_btn);
    m_layout->addWidget(cancel_btn);
    this->setLayout(m_layout);
}

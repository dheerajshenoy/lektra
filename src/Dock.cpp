#include "Dock.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QSlider>
#include <QSpinBox>

Dock::Dock(const QString &title, Qt::Orientation layout,
           QWidget *parent) noexcept
    : QDockWidget(title, parent)
{
    m_container = new QWidget(this);

    QBoxLayout *qlayout
        = (layout == Qt::Horizontal)
              ? static_cast<QBoxLayout *>(new QHBoxLayout(m_container))
              : static_cast<QBoxLayout *>(new QVBoxLayout(m_container));

    m_container->setLayout(qlayout);
    setWidget(m_container);
}

QLayout *
Dock::current_layout() const noexcept
{
    if (!m_layout_stack.empty())
        return m_layout_stack.back();

    return m_container->layout();
}

QPushButton *
Dock::add_button(const QString &label, std::function<void()> callback) noexcept
{
    QPushButton *button = new QPushButton(label, this);
    connect(button, &QPushButton::clicked, this, [callback]()
    {
        if (callback)
            callback();
    });

    current_layout()->addWidget(button);
    return button;
}

QLabel *
Dock::add_label(const QString &label) noexcept
{
    QLabel *qlabel = new QLabel(label, this);
    current_layout()->addWidget(qlabel);
    return qlabel;
}

QTextEdit *
Dock::add_textedit(const QString &placeholder) noexcept
{
    QTextEdit *textarea = new QTextEdit(this);
    textarea->setPlaceholderText(placeholder);
    current_layout()->addWidget(textarea);
    return textarea;
}

QLineEdit *
Dock::add_lineedit(const QString &placeholder,
                   std::function<void(const QString &)> callback) noexcept
{
    QLineEdit *input = new QLineEdit(this);
    input->setPlaceholderText(placeholder);
    connect(input, &QLineEdit::returnPressed, this, [input, callback]()
    {
        if (callback)
            callback(input->text());
        input->clear();
    });

    current_layout()->addWidget(input);
    return input;
}

QCheckBox *
Dock::add_checkbox(const QString &label, bool checked,
                   std::function<void(bool)> callback) noexcept
{
    QCheckBox *checkbox = new QCheckBox(label, this);
    checkbox->setChecked(checked);
    connect(checkbox, &QCheckBox::toggled, this, [callback](bool checked)
    {
        if (callback)
            callback(checked);
    });

    current_layout()->addWidget(checkbox);
    return checkbox;
}

QComboBox *
Dock::add_combobox(const QStringList &items,
                   std::function<void(const QString &)> callback) noexcept
{
    QComboBox *combobox = new QComboBox(this);
    combobox->addItems(items);
    connect(combobox, &QComboBox::currentTextChanged, this,
            [callback](const QString &text)
    {
        if (callback)
            callback(text);
    });

    current_layout()->addWidget(combobox);
    return combobox;
}

QSpinBox *
Dock::add_spinbox(int min, int max, int value,
                  std::function<void(int)> callback) noexcept
{
    QSpinBox *spinbox = new QSpinBox(this);
    spinbox->setRange(min, max);
    spinbox->setValue(value);
    connect(spinbox, &QSpinBox::valueChanged, this, [callback](int value)
    {
        if (callback)
            callback(value);
    });

    current_layout()->addWidget(spinbox);
    return spinbox;
}

QSlider *
Dock::add_slider(Qt::Orientation orientation, int min, int max, int value,
                 std::function<void(int)> callback) noexcept
{
    QSlider *slider = new QSlider(orientation, this);
    slider->setRange(min, max);
    slider->setValue(value);
    connect(slider, &QSlider::valueChanged, this, [callback](int value)
    {
        if (callback)
            callback(value);
    });

    current_layout()->addWidget(slider);
    return slider;
}

QProgressBar *
Dock::add_progressbar(int min, int max) noexcept
{
    QProgressBar *progressbar = new QProgressBar(this);
    progressbar->setRange(min, max);
    current_layout()->addWidget(progressbar);
    return progressbar;
}

void
Dock::begin_container(Qt::Orientation orientation, int spacing) noexcept
{
    QWidget *box = new QWidget(this);
    QBoxLayout *box_layout
        = (orientation == Qt::Horizontal)
              ? static_cast<QBoxLayout *>(new QHBoxLayout(box))
              : static_cast<QBoxLayout *>(new QVBoxLayout(box));

    if (spacing >= 0)
        box_layout->setSpacing(spacing);

    box->setLayout(box_layout);
    current_layout()->addWidget(box);

    m_layout_stack.push_back(box_layout);
}

void
Dock::end_container() noexcept
{
    if (!m_layout_stack.empty())
        m_layout_stack.pop_back();
}

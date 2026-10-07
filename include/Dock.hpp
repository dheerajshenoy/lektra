#pragma once

#include <QBoxLayout>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <functional>
#include <vector>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QSlider;
class QProgressBar;

class Dock : public QDockWidget
{
    Q_OBJECT

public:
    Dock(const QString &title, Qt::Orientation layout,
         QWidget *parent = nullptr) noexcept;

    QPushButton *add_button(const QString &label,
                            std::function<void()> callback) noexcept;
    QLabel *add_label(const QString &label) noexcept;
    QTextEdit *add_textedit(const QString &placeholder) noexcept;
    QLineEdit *
    add_lineedit(const QString &placeholder,
                 std::function<void(const QString &)> callback) noexcept;
    QCheckBox *add_checkbox(const QString &label, bool checked,
                            std::function<void(bool)> callback) noexcept;
    QComboBox *
    add_combobox(const QStringList &items,
                 std::function<void(const QString &)> callback) noexcept;
    QSpinBox *add_spinbox(int min, int max, int value,
                          std::function<void(int)> callback) noexcept;
    QSlider *add_slider(Qt::Orientation orientation, int min, int max,
                        int value, std::function<void(int)> callback) noexcept;
    QProgressBar *add_progressbar(int min, int max) noexcept;

    // Opens a new box-layout container (added to whatever container is
    // currently on top of the stack) and pushes it as the new target for
    // subsequent add_*() calls. Pair with end_container().
    void begin_container(Qt::Orientation orientation, int spacing) noexcept;
    // Pops the container opened by the matching begin_container().
    void end_container() noexcept;

private:
    QLayout *current_layout() const noexcept;

    QWidget *m_container = nullptr;
    std::vector<QBoxLayout *> m_layout_stack;
};

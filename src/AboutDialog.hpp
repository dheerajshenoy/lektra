#pragma once

#include <QDialog>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

private:
    QWidget *softwaresUsedSection() noexcept;
    QWidget *authorsSection() noexcept;

    QLabel *infoLabel;
    QPushButton *closeButton;
    QTabWidget *m_tabWidget;
};

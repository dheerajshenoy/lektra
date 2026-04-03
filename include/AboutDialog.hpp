#pragma once

#include <QDialog>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

extern "C"
{
#include <mupdf/fitz.h>
#ifdef HAS_SYNCTEX
    #include <synctex/synctex_version.h>
#endif
#ifdef HAS_DJVU
    #include <libdjvu/ddjvuapi.h>
#endif
}

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

private:
    QWidget *softwaresUsedSection() noexcept;
    QWidget *authorsSection() noexcept;

private:
    QLabel *infoLabel        = nullptr;
    QPushButton *closeButton = nullptr;
    QTabWidget *m_tabWidget  = nullptr;
};

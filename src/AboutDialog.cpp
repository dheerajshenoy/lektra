#include "AboutDialog.hpp"

#include <QFormLayout>
#include <QPainter>
#include <QTextEdit>
#include <qboxlayout.h>
#include <qfont.h>
#include <qnamespace.h>

extern "C"
{
#include <mupdf/fitz.h>
#ifdef HAS_SYNCTEX
#include <synctex/synctex_version.h>
#endif
}

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent), infoLabel(new QLabel),
      closeButton(new QPushButton("Close"))
{

    setWindowTitle("About");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint
                   & ~Qt::WindowMaximizeButtonHint);

    setMinimumSize(600, 400);

    // Setup logo font - load from resources
    int fontId = QFontDatabase::addApplicationFont(
        ":/resources/fonts/Major-Mono-Display.ttf");
    QString fontFamily
        = QFontDatabase::applicationFontFamilies(fontId).value(0, QString());
    QFont logoFont;
    if (!fontFamily.isEmpty())
        logoFont.setFamily(fontFamily);
    logoFont.setPointSize(35);
    logoFont.setBold(true);

    QLabel *bannerText = new QLabel("lektra");
    bannerText->setContentsMargins(0, 0, 0, 0);

    bannerText->setAutoFillBackground(true);
    bannerText->setStyleSheet(
        "QLabel { background-color : black; color : pink; }");

    bannerText->setFont(logoFont);
    bannerText->setContentsMargins(10, 50, 50, 10);

    m_tabWidget = new QTabWidget(this);

    QVBoxLayout *otherLayout = new QVBoxLayout();
    otherLayout->addWidget(bannerText);

    QTextEdit *licenseTextEdit = new QTextEdit();
    licenseTextEdit->setReadOnly(true);

    QFile file(":/LICENSE"); // Or use an absolute/relative path

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&file);
        QString text = in.readAll();
        licenseTextEdit->setPlainText(text);
        file.close();
    }
    else
    {
        licenseTextEdit->setPlainText("Could not load license text.");
    }

    auto *layout = new QVBoxLayout();
    layout->addLayout(otherLayout);
    layout->addWidget(m_tabWidget);
    layout->addWidget(closeButton, 0, Qt::AlignCenter);
    layout->setContentsMargins(0, 0, 0, 0);

    setLayout(layout);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    QWidget *authorWidget = authorsSection();
    m_tabWidget->addTab(authorWidget, "About");

    QWidget *softwaresUsed = softwaresUsedSection();
    m_tabWidget->addTab(softwaresUsed, "Libraries Used");
    m_tabWidget->addTab(licenseTextEdit, "License");
    setWindowModality(Qt::NonModal);
}

QWidget *
AboutDialog::softwaresUsedSection() noexcept
{
    QWidget *widget     = new QWidget();
    QFormLayout *layout = new QFormLayout();

    QVBoxLayout *outerLayout = new QVBoxLayout();
    layout->setAlignment(Qt::AlignCenter);

    layout->addRow("Qt", new QLabel(QT_VERSION_STR));
    layout->addRow("MuPDF", new QLabel(QString(FZ_VERSION)));
#ifdef HAS_SYNCTEX
    layout->addRow("SyncTeX", new QLabel(QString(SYNCTEX_VERSION_STRING)));
#endif

    outerLayout->addLayout(layout, Qt::AlignCenter);
    widget->setLayout(outerLayout);
    return widget;
}

QWidget *
AboutDialog::authorsSection() noexcept

{
    QWidget *widget = new QWidget(this);

    QFormLayout *layout = new QFormLayout(widget);
    layout->addRow("Version", new QLabel(APP_VERSION));
    layout->addRow("Created by", new QLabel("Dheeraj Vittal Shenoy"));
    layout->addRow("Github",
                   new QLabel("<a href='https://codeberg.org/lektra/lektra'>https://codeberg.org/lektra/lektra</a>"));
    widget->setLayout(layout);

    return widget;
}

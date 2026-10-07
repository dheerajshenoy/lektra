#include "DonateDialog.hpp"

#include <QDesktopServices>
#include <QStyle>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

DonateDialog::DonateDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Support Lektra"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint
                   & ~Qt::WindowMaximizeButtonHint);
    setMinimumWidth(420);

    auto *icon  = new QLabel;
    icon->setPixmap(
        style()->standardIcon(QStyle::SP_MessageBoxInformation)
            .pixmap(48, 48));
    icon->setAlignment(Qt::AlignTop);

    auto *heading = new QLabel(tr("<b>Support Lektra Development</b>"));
    heading->setAlignment(Qt::AlignLeft);

    auto *body = new QLabel(
        tr("Lektra is developed by Dheeraj Vittal Shenoy and contributors. "
           "It is free and open-source software and will stay free forever.\n\n"
           "If you find Lektra useful, consider supporting its development. "
           "Every contribution helps keep the project alive. "
           "You can donate through any one of the services below."));
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignLeft);

    auto *kofiBtn  = new QPushButton(tr("Ko-fi"));
    auto *lpBtn    = new QPushButton(tr("Liberapay"));
    auto *ghBtn    = new QPushButton(tr("GitHub Sponsors"));
    auto *closeBtn = new QPushButton(tr("Close"));

    connect(kofiBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl("https://ko-fi.com/dheerajshenoy"));
    });
    connect(lpBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl("https://liberapay.com/dheerajshenoy"));
    });
    connect(ghBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl("https://github.com/sponsors/dheerajshenoy"));
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *donateRow = new QHBoxLayout;
    donateRow->addWidget(kofiBtn);
    donateRow->addWidget(lpBtn);
    donateRow->addWidget(ghBtn);

    auto *closeRow = new QHBoxLayout;
    closeRow->addStretch();
    closeRow->addWidget(closeBtn);

    auto *textCol = new QVBoxLayout;
    textCol->addWidget(heading);
    textCol->addSpacing(6);
    textCol->addWidget(body);
    textCol->addSpacing(12);
    textCol->addLayout(donateRow);
    textCol->addSpacing(8);
    textCol->addLayout(closeRow);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(icon);
    topRow->addSpacing(12);
    topRow->addLayout(textCol);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->addLayout(topRow);
}

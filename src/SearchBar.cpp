#include "SearchBar.hpp"

#include <QHBoxLayout>
#include <QMessageBox>
#include <QStyle>

SearchBar::SearchBar(QWidget *parent) : QWidget(parent)
{
    m_spinner = new WaitingSpinnerWidget(this, false, true);
    m_spinner->setInnerRadius(5);

    // Set color based on the current palette's text color
    m_spinner->setColor(palette().color(QPalette::Text));
    m_label            = new QLabel("Search:", this);
    m_searchInput      = new QLineEdit(this);
    m_prevButton       = new QPushButton(this);
    m_nextButton       = new QPushButton(this);
    m_closeButton      = new QPushButton(this);
    m_searchCountLabel = new QLabel(this);
    m_searchIndexLabel = new QLineEdit(this);

    m_searchInput->setFocusPolicy(Qt::ClickFocus);

    m_nextButton->setToolTip("Goto Next Hit");
    m_prevButton->setToolTip("Goto Previous Hit");
    m_closeButton->setToolTip("Close Search Bar");

    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_prevButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    m_closeButton->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->addWidget(m_spinner);
    layout->addWidget(m_label);
    layout->addWidget(m_searchInput, 1);
    layout->addWidget(m_searchIndexLabel);
    layout->addWidget(m_searchSeparator);
    layout->addWidget(m_searchCountLabel);
    layout->addWidget(m_prevButton);
    layout->addWidget(m_nextButton);
    layout->addWidget(m_closeButton);

    m_searchIndexLabel->setSizePolicy(QSizePolicy::Maximum,
                                      QSizePolicy::Preferred);
    m_searchInput->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Preferred);
    m_searchInput->setPlaceholderText("Search");
    m_searchIndexLabel->hide();
    m_searchSeparator->hide();
    m_searchCountLabel->hide();

    connect(m_searchInput, &QLineEdit::returnPressed, this, [this]()
    {
        m_searchInput->clearFocus();
        const QString searchText = m_searchInput->text();
        search(searchText);
    });

    connect(m_searchIndexLabel, &QLineEdit::returnPressed, this, [this]()
    {
        m_searchInput->clearFocus();
        bool ok;
        int index = m_searchIndexLabel->text().toInt(&ok);
        if (ok)
            emit searchIndexChangeRequested(index - 1);
        else
        {
            QMessageBox::warning(this, "Invalid Index",
                                 "Please enter a valid search index.");
        }
    });

    connect(m_nextButton, &QPushButton::clicked, this,
            [this]() { emit nextHitRequested(); });

    connect(m_prevButton, &QPushButton::clicked, this,
            [this]() { emit prevHitRequested(); });

    connect(m_closeButton, &QPushButton::clicked, this, [this]()
    {
        m_searchInput->clearFocus();
        this->hide();
    });
}

void
SearchBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_searchInput->setFocus();
}

void
SearchBar::setSearchCount(int count) noexcept
{
    if (count > 0)
    {
        m_searchCountLabel->setText(QString::number(count));
        m_searchCountLabel->show();
    }
}

void
SearchBar::setSearchIndex(int index) noexcept
{
    m_searchIndexLabel->setText(QString::number(index + 1));
    m_searchIndexLabel->show();
    m_searchSeparator->show();
}

void
SearchBar::search(const QString &term) noexcept
{
    emit searchRequested(term);
    if (term.isEmpty())
        this->hide();
}

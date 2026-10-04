#include "Lektra.hpp"

#include "NoticeLogic.hpp"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSaveFile>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr const char *kLatestReleaseApi
    = "https://api.github.com/repos/dheerajshenoy/lektra/releases/latest";
constexpr const char *kReleasesPage
    = "https://github.com/dheerajshenoy/lektra/releases";
// An automatic check is made at most once a day (a little less, so a daily
// user is not skipped by a few minutes).
constexpr int kUpdateCheckHours = 22;

QDateTime
readTime(const QJsonObject &state, const char *key)
{
    return QDateTime::fromString(state.value(QLatin1String(key)).toString(),
                                 Qt::ISODate);
}

void
writeTime(QJsonObject &state, const char *key, const QDateTime &time)
{
    state[QLatin1String(key)] = time.toUTC().toString(Qt::ISODate);
}
} // namespace

// What Lektra remembers between runs for the banners: when it was first used,
// how often it was started, which version last ran, and what was shown.
void
Lektra::loadAppState() noexcept
{
    QFile file(m_app_data_dir.filePath("app_state.json"));
    if (file.open(QIODevice::ReadOnly))
        m_app_state = QJsonDocument::fromJson(file.readAll()).object();
}

void
Lektra::saveAppState() noexcept
{
    QSaveFile file(m_app_data_dir.filePath("app_state.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(m_app_state).toJson());
    file.commit();
}

// `firstRun`: Lektra has never been started on this machine before.
void
Lektra::initNotices(bool firstRun) noexcept
{
    loadAppState();
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (!readTime(m_app_state, "first_run").isValid())
    {
        // Someone who used Lektra before this existed is dated from when the
        // first-run marker was written.
        QDateTime first = now;
        const QFileInfo marker(m_app_data_dir.filePath(".first_run_done"));
        if (!firstRun && marker.exists())
        {
            // The earlier of the two times: a restored or copied file can
            // have a recent creation time.
            first = marker.lastModified();
            if (marker.birthTime().isValid() && marker.birthTime() < first)
                first = marker.birthTime();
        }
        writeTime(m_app_state, "first_run", first);
    }
    m_app_state["launches"] = m_app_state.value("launches").toInt() + 1;

    const QString current  = QStringLiteral(APP_VERSION);
    const bool updated     = !firstRun && m_app_state.value("last_version").toString() != current;
    m_app_state["last_version"] = current;
    saveAppState();

    bool noticeShown = false;

    if (updated && m_config.updates.whats_new)
    {
        noticeShown = true;
        QTimer::singleShot(1500, this, [this, current]
        {
            m_notice_bar->post(
                {tr("Lektra was updated to %1.").arg(current),
                 {{tr("What's new"), [this] { showWhatsNew(); }}},
                 {}});
        });
    }

    if (m_config.updates.check)
    {
        const QDateTime last = readTime(m_app_state, "update_last_check");
        if (!last.isValid() || last.secsTo(now) >= kUpdateCheckHours * 3600)
            QTimer::singleShot(6000, this, [this] { checkForUpdates(false); });
    }

    // The support reminder waits its turn: never with another banner.
    QTimer::singleShot(12000, this, [this, noticeShown]
    {
        if (noticeShown || m_notice_bar->busy())
            return;
        notice::DonateState state;
        state.firstRun  = readTime(m_app_state, "first_run");
        state.lastShown = readTime(m_app_state, "donate_last_shown");
        state.launches  = m_app_state.value("launches").toInt();
        state.dismissed = m_app_state.value("donate_dismissed").toBool();
        if (!notice::donateReminderDue(state, QDateTime::currentDateTimeUtc(),
                                       m_config.donate.reminders))
            return;

        writeTime(m_app_state, "donate_last_shown", QDateTime::currentDateTimeUtc());
        saveAppState();
        m_notice_bar->post(
            {tr("Lektra is free and made in spare time. If it is useful to "
                "you, please consider supporting it."),
             {{tr("Support Lektra"), [this] { ShowDonate(); }},
              {tr("Maybe later"), {}},
              {tr("Don't ask again"),
               [this]
        {
            m_app_state["donate_dismissed"] = true;
            saveAppState();
        }}},
             {}});
    });
}

// Asks GitHub for the latest release. `manual`: the user asked, so the answer
// is always given, including "up to date" and errors.
void
Lektra::checkForUpdates(bool manual) noexcept
{
    if (!m_update_net)
        m_update_net = new QNetworkAccessManager(this);

    QNetworkRequest request{QUrl(QLatin1String(kLatestReleaseApi))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "lektra/" APP_VERSION);
    request.setTransferTimeout(10000);

    QNetworkReply *reply = m_update_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual]
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            if (manual)
                m_message_bar->showMessage(
                    tr("Could not check for updates: %1").arg(reply->errorString()),
                    4.0f);
            return;
        }

        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag         = release.value("tag_name").toString();
        const QString url         = release.value("html_url").toString();
        if (tag.isEmpty())
        {
            if (manual)
                m_message_bar->showMessage(tr("Could not read the latest release"), 4.0f);
            return;
        }

        writeTime(m_app_state, "update_last_check", QDateTime::currentDateTimeUtc());
        saveAppState();

        const QString current = QStringLiteral(APP_VERSION);
        if (!notice::isNewerVersion(tag, current))
        {
            if (manual)
                m_message_bar->showMessage(
                    tr("Lektra is up to date (version %1)").arg(current), 3.0f);
            return;
        }
        if (!manual && m_app_state.value("update_skipped").toString() == tag)
            return;

        QString shownTag = tag;
        if (shownTag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
            shownTag.remove(0, 1);
        m_notice_bar->post(
            {tr("Lektra %1 is available (you have %2).").arg(shownTag, current),
             {{tr("Release notes"),
               [url] { QDesktopServices::openUrl(QUrl(url.isEmpty() ? QLatin1String(kReleasesPage) : url)); }},
              {tr("Skip this version"),
               [this, tag]
        {
            m_app_state["update_skipped"] = tag;
            saveAppState();
        }}},
             {}});
    });
}

// The changes of this version, taken from the changelog that is built in.
void
Lektra::showWhatsNew() noexcept
{
    const QString current = QStringLiteral(APP_VERSION);
    QFile file(QStringLiteral(":/CHANGELOG.md"));
    const QString changes
        = file.open(QIODevice::ReadOnly)
              ? notice::changelogSection(QString::fromUtf8(file.readAll()), current)
              : QString();

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("What's new in Lektra %1").arg(current));
    dialog->resize(680, 540);

    auto *layout  = new QVBoxLayout(dialog);
    auto *browser = new QTextBrowser(dialog);
    browser->setOpenExternalLinks(true);
    if (changes.isEmpty())
        browser->setMarkdown(
            tr("The list of changes is not available here. See the [release "
               "notes](%1).").arg(QLatin1String(kReleasesPage)));
    else
        browser->setMarkdown(changes);
    layout->addWidget(browser, 1);

    auto *buttons = new QDialogButtonBox(dialog);
    auto *support = buttons->addButton(tr("Support Lektra"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(support, &QPushButton::clicked, this, [this] { ShowDonate(); });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);

    dialog->open();
}

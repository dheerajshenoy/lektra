#pragma once

#include <QApplication>
#include <QDrag>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QStyle>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QToolButton>
#include <QVariant>
#include <QVector>

class TabBar : public QTabBar
{
    Q_OBJECT

public:
    explicit TabBar(QWidget *parent = nullptr);

    static constexpr const char *MIME_TYPE = "application/lektra-tab";

    enum class CloseButtonMode
    {
        All = 0,
        Current,
        Hidden
    };

    // Switches between Qt's native tabsClosable() (All/Hidden) and manually
    // attaching a single close button to just the current tab (Current).
    void setCloseButtonMode(CloseButtonMode mode) noexcept;

    struct TabData
    {
        QString filePath;
        int currentPage  = 1;
        double zoom      = 1.0;
        bool invertColor = false;
        int rotation     = 0;
        int fitMode      = 0;

        QByteArray serialize() const noexcept
        {
            QJsonObject obj;
            obj["file_path"]    = filePath;
            obj["current_page"] = currentPage;
            obj["zoom"]         = zoom;
            obj["invert_color"] = invertColor;
            obj["rotation"]     = rotation;
            obj["fit_mode"]     = fitMode;
            return QJsonDocument(obj).toJson(QJsonDocument::Compact);
        }

        static TabData deserialize(const QByteArray &data) noexcept
        {
            TabData result;
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
                return result;

            QJsonObject obj    = doc.object();
            result.filePath    = obj["file_path"].toString();
            result.currentPage = obj["current_page"].toInt(1);
            result.zoom        = obj["zoom"].toDouble(1.0);
            result.invertColor = obj["invert_color"].toBool(false);
            result.rotation    = obj["rotation"].toInt(0);
            result.fitMode     = obj["fit_mode"].toInt(0);
            return result;
        }
    };

    void set_split_count(int index, int count) noexcept;
    int splitCount(int index) const noexcept;

    // Marks a tab as failed-to-open — its title is repainted in red,
    // manually overdrawn on top of the normal text. Not done via
    // setTabTextColor(): some Qt platform themes ignore per-tab palette
    // overrides and always paint tab text in the theme's own color, so that
    // API silently has no visual effect under those styles.
    void setTabFailed(int index, bool failed) noexcept;

    // Multi-selection: Ctrl+click toggles a tab, Shift+click selects a range
    // from the last Ctrl/plain-selected tab. Plain clicks clear it.
    QList<int> selectedTabs() const;
    void clearTabSelection() noexcept;
    void setTabSelected(int index, bool selected) noexcept;
    void selectAllTabs() noexcept;
    bool isTabFailed(int index) const noexcept;

signals:
    void tabDataRequested(int index, TabData *outData);
    void tabDropReceived(const TabData &data);
    void tabDetached(int index, const QPoint &globalPos);
    void tabDetachedToNewWindow(int index, const TabData &data);
    void contextMenuRequested(int index, const QPoint &globalPos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    QSize tabSizeHint(int index) const override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void tabInserted(int index) override;
    void tabRemoved(int index) override;
    void tabMoved(int from, int to);

private:
    // Re-applies m_close_button_mode's per-tab button placement. A no-op
    // unless mode == Current — All/Hidden are handled entirely by Qt's own
    // setTabsClosable(), no per-tab bookkeeping needed.
    void refreshCloseButtons() noexcept;

    QPoint m_drag_start_pos;
    int m_drag_tab_index = -1;
    QVector<int> m_split_counts;
    CloseButtonMode m_close_button_mode = CloseButtonMode::All;
    QSet<int> m_failed_tabs;
    QSet<int> m_selected_tabs;
    int m_selection_anchor = -1;
};

#pragma once

#include <QApplication>
#include <QDrag>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QVariant>

class TabBar : public QTabBar
{
    Q_OBJECT

public:
    static inline std::atomic<bool> s_drop_accepted{false};
    static constexpr const char *MIME_TYPE = "application/lektra-tab";
    explicit TabBar(QWidget *parent = nullptr);
    struct TabData
    {
        QString filePath;
        int currentPage{1};
        double zoom{1.0};
        bool invertColor{false};
        int rotation{0};
        int fitMode{0};

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

signals:
    void tabDataRequested(int index, TabData *outData);
    void tabDropReceived(const TabData &data);
    void tabDetached(int index, const QPoint &globalPos);
    void tabDetachedToNewWindow(int index, const TabData &data);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QPoint m_drag_start_pos;
    int m_drag_tab_index{-1};
};

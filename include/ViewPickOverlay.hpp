#pragma once

#include <QList>
#include <QPointer>
#include <QSet>
#include <QWidget>

class DocumentContainer;
class DocumentView;

// Covers a DocumentContainer and lets the user pick some of its views: every
// view gets a number, the digit keys toggle a view, Enter confirms and Escape
// cancels. Deletes itself when done.
class ViewPickOverlay : public QWidget
{
    Q_OBJECT

public:
    // `preselected` views start out selected.
    explicit ViewPickOverlay(
        DocumentContainer *container,
        const QList<QPointer<DocumentView>> &preselected = {});

    // Toggles the view drawn with `number` (1-based).
    void toggle(int number) noexcept;
    QList<DocumentView *> selected() const noexcept;

signals:
    void accepted(const QList<DocumentView *> &views);
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void finish(bool accept) noexcept;

    QPointer<DocumentContainer> m_container;
    QSet<DocumentView *> m_selected;
    bool m_done = false;
};

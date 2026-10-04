#pragma once

#include "ChatBubble.hpp"
#include "ChatInput.hpp"
#include "ChatStore.hpp"
#include "Config.hpp"
#include "HTTPClient.hpp"

#include <QDockWidget>
#include <functional>
#include <QPushButton>
#include <QToolButton>
#include <QTextEdit>
#include <QVBoxLayout>

class QFrame;
class QMenu;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QTimer;

// Outcome of running one Lua script from the chat.
struct LLMScriptResult
{
    bool ok = false;
    QString output; // what the script printed
    QString value;  // what it returned
    QString error;  // message when !ok
};
using LLMScriptRunner = std::function<LLMScriptResult(const QString &code)>;

class LLMView : public QDockWidget
{
public:
    LLMView(const Config &config, QWidget *parent = nullptr);

    // Aborts any in-flight request. Call this before the application
    // closes so a pending request doesn't keep the connection open past
    // shutdown.
    void closeConnection() noexcept;

    // Text sent to the model as the system message with every request (e.g.
    // instructions and the Lua API reference).
    void setSystemPromptProvider(std::function<QString()> provider);
    // Lets replies that contain a ```lua block be run from the chat. Without
    // a runner no Run button is shown.
    void setScriptRunner(LLMScriptRunner runner);
    // Folder where chats are saved (and listed in the History menu). Without
    // one, or with llm_view.save_history off, nothing is saved.
    void setHistoryFolder(const QString &folder);

    // Where "Attach page" and "Attach region" get their images from (the open
    // document). Without them those two menu entries are disabled.
    struct ImageSources
    {
        std::function<QImage()> currentPage;
        // Lets the user pick a part of the page, then calls back with it.
        std::function<void(std::function<void(const QImage &)> done)> pickRegion;
    };
    void setImageSources(ImageSources sources);

    // Attach an image to the message being written. Returns false if it
    // could not be attached (not an image, or too many already).
    bool attachImage(const QImage &image);
    bool attachFile(const QString &path);

protected:
    // Installed on m_input_edit so Shift+Return sends the message instead
    // of inserting a newline (plain Return still inserts a newline, the
    // QTextEdit default). Also tracks focus to highlight the input frame.
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Dropping an image or an image file anywhere on the panel attaches it.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    // Re-derives the input colours and send icon when the palette changes
    // (e.g. light/dark theme switch).
    void changeEvent(QEvent *event) override;

private:
    void initUI();
    void sendMessage();
    void displayResponse(const QString &response);
    // Appends one incremental streamed fragment (extra_body.stream = true)
    // to the display as it arrives, instead of waiting for the full reply.
    void appendStreamChunk(const QString &deltaText);
    void displayError(const QString &message);
    // Disables the Send button and shows/hides the "Thinking..." label for
    // the duration of one request/response exchange.
    void setAwaitingResponse(bool awaiting);
    // Adds a widget as the last message in the transcript and scrolls to it.
    void addBubble(QWidget *bubble) noexcept;
    // Under a finished reply: one "Run / Copy" bar per ```lua block (and runs
    // them straight away when llm_view.auto_run is on).
    void addScriptActions(const QString &reply, bool allowAutoRun = true);
    void runScript(const QString &code, QPushButton *runButton);
    void scrollToBottom() noexcept;
    // Reflects an HTTPClient::connectionStatusChanged() result in
    // m_connection_indicator.
    void updateConnectionIndicator(bool connected) noexcept;
    // Rich-text tooltip for the connection indicator: model, provider,
    // endpoint, key, streaming, last check and the state of this chat.
    QString connectionTooltip() const;
    // Colours of the input frame and send button, taken from the palette so
    // they follow the theme.
    void updateInputStyle() noexcept;
    void rebuildAttachmentBar();
    void clearAttachments();
    // A short message under the transcript that goes away by itself.
    void flashNote(const QString &note);
    // The conversation as it is saved: images are replaced by a note, so saved
    // chats stay small and old images are never sent again.
    static QJsonArray withoutImages(const QJsonArray &messages);
    // The send button is only enabled when there is text and no request in
    // flight.
    void updateSendEnabled() noexcept;
    // Grows the input with its text, from one line up to a few, then scrolls.
    void adjustInputHeight() noexcept;

    HTTPClient *m_http_client       = nullptr;
    QScrollArea *m_scroll_area      = nullptr;
    QWidget *m_messages_widget      = nullptr;
    QVBoxLayout *m_messages_layout  = nullptr;
    QFrame *m_input_frame           = nullptr;
    ChatInput *m_input_edit         = nullptr;
    QToolButton *m_send_button      = nullptr;
    QToolButton *m_attach_button    = nullptr;
    QMenu *m_attach_menu            = nullptr;
    QWidget *m_attachment_bar       = nullptr;
    QHBoxLayout *m_attachment_layout = nullptr;
    QList<QImage> m_attachments;
    ImageSources m_image_sources;
    bool m_updating_style           = false;
    QLabel *m_status_label          = nullptr;
    // Shows "Connected"/"Disconnected" for the configured LLM endpoint,
    // refreshed by m_connection_check_timer.
    QLabel *m_connection_indicator      = nullptr;
    QTimer *m_connection_check_timer    = nullptr;
    QVBoxLayout *m_layout           = nullptr;
    QWidget *m_container            = nullptr;
    // The in-progress assistant bubble while a streamed reply is arriving —
    // nullptr when no stream is active. Only this one bubble is touched per
    // chunk, unlike the old whole-transcript re-render.
    ChatBubble *m_active_assistant_bubble = nullptr;
    // Markdown source accumulated for the in-progress streamed bubble only
    // (not the whole conversation) — reset per exchange.
    QString m_streaming_markdown;
    // True while a streamed response is being appended chunk-by-chunk, so
    // displayResponse() (which always fires once, streamed or not) knows
    // the bubble was already created/filled and doesn't add a second one.
    bool m_streaming_active  = false;
    // True from sendMessage() until the exchange fully completes (reply or
    // error) — kept disabled through the whole streamed response, not just
    // until the first chunk, because HTTPClient only tracks one in-flight
    // request: a second send() while one is still streaming would corrupt
    // its SSE parsing state.
    bool m_awaiting_response = false;

    // --- chat history ---------------------------------------------------
    // Starts an empty chat (the current one stays saved).
    void newChat();
    // Shows a saved chat and continues from it.
    void loadChat(const QString &id);
    void deleteCurrentChat();
    // Remembers what the transcript shows, then writes the chat to disk.
    void record(const QString &kind, const QString &text, bool save = true,
                int images = 0);
    void saveChat();
    void clearTranscriptWidgets();
    void refreshHistoryMenu();
    void updateChatButtons();

    // The last connection check (see HTTPClient::connectionChecked).
    bool m_conn_known       = false;
    bool m_conn_ok          = false;
    int m_conn_status       = 0;
    qint64 m_conn_latency   = 0;
    QString m_conn_error;
    QDateTime m_conn_time;

    ChatStore m_store;
    QMenu *m_history_menu             = nullptr;
    QToolButton *m_history_button     = nullptr;
    QToolButton *m_new_chat_button    = nullptr;
    QString m_chat_id;                // empty until the first message
    QString m_chat_title;
    QDateTime m_chat_created;
    QJsonArray m_transcript;

    LLMScriptRunner m_script_runner;
    // What the last script did, handed to the model with the next message so
    // it knows whether its script worked.
    QString m_pending_result;

    const Config &m_config;
};

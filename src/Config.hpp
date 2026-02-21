#pragma once

#include "DocumentView.hpp"
#include "GraphicsView.hpp"
#include "TabWidget.hpp"

#include <QColor>
#include <QHash>
#include <Qt>
#include <array>

struct Config
{
    QHash<QString, QString> shortcuts{};

    struct colors
    {
        uint32_t accent{0x3daee9FF};
        uint32_t background{0x00000000};
        uint32_t page_background{0xFFFFFFFF};
        uint32_t page_foreground{0x000000FF};
        uint32_t search_match{0x55500033};
        uint32_t search_index{0x55FF0055};
        uint32_t link_hint_bg{0x000000FF};
        uint32_t link_hint_fg{0xea3ee9FF};
        uint32_t selection{0x0000FF55};
        uint32_t highlight{0x55FF0055};
        uint32_t jump_marker{0xFF0000FF};
        uint32_t annot_rect{0x55FF5588};
        uint32_t annot_popup{0xFFFFFFAA};
    } colors{};

    struct window
    {
        bool fullscreen{false};
        bool menubar{true};
        bool startup_tab{true};
        QString title_format{"{} - lektra"};
        std::tuple<int, int> initial_size{-1,
                                          -1}; // width, height; -1 for default
    } window{};

    struct layout
    {
        DocumentView::LayoutMode mode{DocumentView::LayoutMode::TOP_TO_BOTTOM};
        DocumentView::FitMode initial_fit{DocumentView::FitMode::Width};
        bool auto_resize{false};
        int spacing{10};
    } layout{};

    struct statusbar
    {
        bool visible{true};
        std::array<int, 4> padding{5, 5, 5, 5}; // top, right, bottom, left
        bool show_session_name{false};
        bool file_name_only{false};
        bool show_file_info{true};
        bool show_page_number{true};
        bool show_mode{true};
        bool show_progress{true};
    } statusbar{};

    struct zoom
    {
        float level{0.5f};
        float factor{1.25f};
    } zoom{};

    struct selection
    {
        int drag_threshold{50};
    } selection{};

    struct split
    {
        bool focus_follows_mouse{true};
        bool dim_inactive{true};
        float dim_inactive_opacity{0.5f}; // 0.0 (no dim) to 1.0 (fully dimmed)
    } split{};

    struct scrollbars
    {
        bool horizontal{true};
        bool vertical{true};
        bool search_hits{true};
        bool auto_hide{true};
        int size{12};
        float hide_timeout{1.5}; // seconds of inactivity before hiding
    } scrollbars{};

    struct markers
    {
        bool jump_marker{true};
    } markers{};

    struct links
    {
        bool boundary{false};
        bool detect_urls{false};
        QString url_regex{R"((https?://|www\.)[^\s<>()\"']+)"};
    } links{};

    struct link_hints
    {
        float size{0.5f};
    } link_hints{};

    struct tabs
    {
        bool visible{true};
        bool auto_hide{false};
        bool closable{true};
        bool movable{true};
        Qt::TextElideMode elide_mode{Qt::TextElideMode::ElideRight};
        QTabWidget::TabPosition location{QTabWidget::TabPosition::North};
        bool full_path{false};
        bool lazy_load{true};
    } tabs{};

    struct outline
    {
    } outline{};

    struct highlight_search
    {
    } highlight_search{};

    struct command_palette
    {
        int width{500};
        int height{300};
        QString placeholder_text{"Type a command..."};
        bool vscrollbar{true};
        bool show_shortcuts{true};
        bool show_grid{false};
    } command_palette{};

    struct picker
    {
        bool border{true};
        struct shadow
        {
            bool enabled{true};
            int blur_radius{18};
            int offset_x{0};
            int offset_y{6};
            int opacity{120};
        } shadow{};
    } picker{};

#ifdef ENABLE_LLM_SUPPORT
    struct llm_widget
    {
        bool visible{false};
        QString panel_position{"right"};
        int panel_width{400};
    } llm_widget{};
#endif

    struct rendering
    {
        float dpi{72.0f};
        std::variant<float, QMap<QString, float>> dpr{1.0f};
        float inv_dpr{1.0f};
        bool icc_color_profile{true};
        bool antialiasing{true};
        bool text_antialiasing{true};
        bool smooth_pixmap_transform{true};
        int antialiasing_bits{8};
    } rendering{};

    struct behavior
    {
        int undo_limit{25};
        int cache_pages{20};
        int preload_pages{5};
        bool auto_reload{true};
        bool config_auto_reload{true};
        bool invert_mode{false};
        bool open_last_visited{false};
        bool always_open_in_new_window{false};
        bool remember_last_visited{true};
        bool recent_files{true};
        int page_history_limit{5};
        int num_recent_files{10};
        int startpage_override{-1};
        GraphicsView::Mode initial_mode{GraphicsView::Mode::TextSelection};
#ifdef HAS_SYNCTEX
        QString synctex_editor_command{QString()};
#endif
        bool confirm_on_quit{true};
    } behavior{};

#ifdef ENABLE_LLM_SUPPORT
    struct llm
    {
        std::string provider{"ollama"};
        std::string model{"llama2-7b-chat"};
        int max_tokens{512};
        // float temperature{0.7f}; //
    } llm{};
#endif
};

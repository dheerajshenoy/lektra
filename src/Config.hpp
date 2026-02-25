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

    // @section Colors
    // @section_note {
    // Colors can be specified in hex format (e.g. #RRGGBBAA) or as a color name
    // }
    // @section_desc Color options struct
    // @section_type struct
    struct Colors
    {
        // @desc Accent color
        // @type str
        // @default "#3daee9FF"
        uint32_t accent{0x3daee9FF};

        // @desc Background color
        // @type str
        // @default "#00000000"
        uint32_t background{0x00000000};

        // @desc Page background color
        // @type str
        // @default "#FFFFFFFF"
        uint32_t page_background{0xFFFFFFFF};

        // @desc Page foreground color
        // @type str
        // @default "#000000FF"
        uint32_t page_foreground{0x000000FF};

        // @desc Search match count color
        // @type str
        // @default "#55500033"
        uint32_t search_match{0x55500033};

        // @desc Search match index color
        // @type str
        // @default "#55FF0055"
        uint32_t search_index{0x55FF0055};

        // @desc Link hint background color
        // @type str
        // @default "#000000FF"
        uint32_t link_hint_bg{0x000000FF};

        // @desc Link hint foreground color
        // @type str
        // @default "#ea3ee9FF"
        uint32_t link_hint_fg{0xea3ee9FF};

        // @desc Selection color
        // @type str
        // @default "#0000FF55"
        uint32_t selection{0x0000FF55};

        // @desc Highlight annotation color
        // @type str
        // @default "#55FF0055"
        uint32_t highlight{0x55FF0055};

        // @desc Jump marker color
        // @type str
        // @default "#FF0000FF"
        uint32_t jump_marker{0xFF0000FF};

        // @desc Rect annotation color
        // @type str
        // @default "#55FF5588"
        uint32_t annot_rect{0x55FF5588};

        // @desc Popup annotation color
        // @type str
        // @default "#FFFFFFAA"
        uint32_t annot_popup{0xFFFFFFAA};

        // @desc Portal border color
        // @type str
        // @default "#FFFFFFAA"
        uint32_t portal_border{0xFFFFFFAA};
    } colors{};
    // @endsection

    struct Portal
    {
        // @desc Enable portal ability
        // @type bool
        // @default true
        bool enabled{true};

        // @desc Portal border width
        // @type int
        // @default 5
        int border_width{5};

        // @desc {
        // Respect parent's destruction (if parent closes, close the
        // portal too)
        // }
        // @type bool
        // @default true
        bool respect_parent{true};

        // @desc Dim inactive for portal views too
        // @type bool
        // @default false
        bool dim_inactive{false};
    } portal{};

    // @section Window
    // @desc Window options struct
    // @type struct
    struct Window
    {
        // @desc Set the window fullscreen
        // @type bool
        // @default false
        bool fullscreen{false};

        // @desc Show the menubar
        // @type bool
        // @default true
        bool menubar{true};

        // @desc Show startup widget tab
        // @type bool
        // @default true
        bool startup_tab{false};

        // @desc Title format for the window title
        // @type str
        // @default "{} - lektra"
        QString title_format{"{} - lektra"};

        // Required for documentation parsing, do not remove
        using WindowSize = std::array<int, 2>;

        // @desc Initial size of the window
        // @type table
        // @default 600,400
        WindowSize initial_size{600, 400}; // width, height; -1 for default
    } window{};
    // @endsection

    // @section Layout
    // @section_desc Layout options struct
    // @section_type struct
    struct Layout
    {

        // @desc Initial page layout mode
        // @type str
        // @choice top_to_bottom, left_to_right, single, book
        // @default top_to_bottom
        DocumentView::LayoutMode mode{DocumentView::LayoutMode::TOP_TO_BOTTOM};

        // @desc Initial page fit mode
        // @default width
        // @choice width, height, window
        DocumentView::FitMode initial_fit{DocumentView::FitMode::Width};

        // @desc Apply auto fit when resizing window
        // @type bool
        // @default false
        bool auto_resize{false};

        // @desc Page spacing in pixels
        // @type int
        // @default 10
        int spacing{10};
    } layout{};
    // @endsection

    // @section Statusbar
    // @section_desc Statusbar options struct
    // @section_type struct
    struct Statusbar
    {
        // @desc Show statusbar
        // @type bool
        // @default true
        bool visible{true};

        using Padding = std::array<int, 4>;
        // @desc Padding
        // @type table
        // @default [5, 5, 5, 5]
        Padding padding{5, 5, 5, 5}; // top, right, bottom, left

        // @desc Show session name (if in session)
        // @type bool
        // @default true
        bool show_session_name{true};

        // @desc Show file name only (as opposed to full path)
        // @type bool
        // @default false
        bool file_name_only{false};

        // @desc Show file info
        // @type bool
        // @default true
        bool show_file_info{true};

        // @desc Show page number
        // @type bool
        // @default true
        bool show_page_number{true};

        // @desc Show interaction mode
        // @type bool
        // @default true
        bool show_mode{true};

        // @desc Show page read progress
        // @type bool
        // @default true
        bool show_progress{true};
    } statusbar{};
    // @endsection

    // @section Zoom
    // @section_desc Zoom options struct
    // @section_type struct
    struct Zoom
    {

        // @desc Default zoom level
        // @type float
        // @default 0.5f
        float level{0.5f};

        // @desc Zoom factor
        // @type float
        // @default 1.25f
        float factor{1.25f};
    } zoom{};
    // @endsection

    // @section Selection
    // @section_desc Selection options struct
    // @section_type struct
    struct Selection
    {
        // @desc Threshold in pixels before actually starting the selection
        // @type int
        // @default 50
        int drag_threshold{50};

        // @desc Copy on text selection
        // @type bool
        // @default false
        bool copy_on_select{false};
    } selection{};
    // @endsection

    // @section Split
    // @section_desc Split options struct
    // @section_type struct
    struct Split
    {
        // @desc Focus of split follows the mouse
        // @type bool
        // @default true
        bool focus_follows_mouse{true};

        // @desc Moving mouse changes the focus
        // @type bool
        // @default true
        bool mouse_follows_focus{true};

        // @desc Dims splits that are not currently focused
        // @type bool
        // @default true
        bool dim_inactive{true};

        // @desc Set the inactive split dim opacity [0-1]
        // @type float
        // @default 0.5f
        float dim_inactive_opacity{0.5f}; // 0.0 (no dim) to 1.0 (fully dimmed)
    } split{};
    // @endsection

    // @section Scrollbars
    // @section_desc Scrollbars options struct
    // @section_type struct
    struct Scrollbars
    {
        // @desc Show the horizontal scrollbar
        // @type bool
        // @default true
        bool horizontal{true};

        // @desc Show the vertical scrollbar
        // @type bool
        // @default true
        bool vertical{true};

        // @desc Show the search hits in scrollbar
        // @type bool
        // @default true
        bool search_hits{true};

        // @desc Auto hide after timeout
        // @type bool
        // @default true
        bool auto_hide{true};

        // @desc Size of the scrollbar in pixels
        // @type int
        // @default 12
        int size{12};

        // @desc Inactive timeout after which to hide the scrollbar
        // @type float
        // @default 1.5
        float hide_timeout{1.5}; // seconds of inactivity before hiding
    } scrollbars{};
    // @endsection

    // @section Markers
    // @section_desc Jump marker options struct
    // @section_type struct
    struct Markers
    {
        // @desc Show the jump marker
        // @type bool
        // @default true
        bool jump_marker{true};
    } markers{};
    // @endsection

    // @section Links
    // @section_desc Links options struct
    // @section_type struct
    struct Links
    {
        // @desc Show the rect boundary for links
        // @type bool
        // @default false
        bool boundary{false};

        // @desc Detect non-pdf link objects that are valid links
        // @type bool
        // @default false
        bool detect_urls{false};

        // @desc Valid regular expression that detects URLs
        // @type str
        // @default R"((https?://|www\.)[^\s<>()\"']+)"
        QString url_regex{R"((https?://|www\.)[^\s<>()\"']+)"};
    } links{};
    // @endsection

    // @section Link Hints
    // @section_desc Link hint options struct
    // @section_type struct
    struct Link_hints
    {
        // @desc Size of the link hint rects
        // @type float
        // @default 0.5f
        float size{0.5f};
    } link_hints{};
    // @endsection

    // @section Tabs
    // @section_desc Tab options struct
    // @section_type struct
    struct Tabs
    {
        // @desc Show tabs
        // @type bool
        // @default true
        bool visible{true};

        // @desc Auto hide tabs when tab count equals 1
        // @type bool
        // @default false
        bool auto_hide{false};

        // @desc Show the tab close buttons
        // @type bool
        // @default true
        bool closable{true};

        // @desc Tabs can be rearranged
        // @type bool
        // @default true
        bool movable{true};

        // @desc Text elide mode when text can't fit in the tab fully
        // @type str
        // @choice right, left, middle, none
        // @default right
        Qt::TextElideMode elide_mode{Qt::TextElideMode::ElideRight};

        // @desc Location of the tabs in the window
        // @type str
        // @choice top, bottom, left, right
        // @default top
        QTabWidget::TabPosition location{QTabWidget::TabPosition::North};

        // @desc Show full file path
        // @type bool
        // @default false
        bool full_path{false};

        // @desc Lazy load tabs
        // @type bool
        // @default true
        bool lazy_load{true};
    } tabs{};
    // @endsection

    // @section Outline
    // @section_desc Outline options struct
    // @section_type struct
    struct Outline
    {
        // @desc Indent width
        // @type int
        // @default 10
        int indent_width{10};

        // @desc Show page numbers
        // @type bool
        // @defaul true
        bool show_page_numbers{true};
    } outline{};
    // @endsection

    // @section Highlight
    // @section_desc Highlight Search options struct
    // @section_type struct
    struct Highlight_search
    {
    } highlight_search{};
    // @endsection

    // @section Command Palette
    // @section_desc Command Palette options struct
    // @section_type struct
    struct Command_palette
    {
        // @desc Width of the command palette picker
        // @type int
        // @default 500
        int width{500};

        // @desc Height of the command palette picker
        // @type int
        // @default 300
        int height{300};

        // @desc Placeholder text for the input field
        // @type str
        // @default "Type a command..."
        QString placeholder_text{"Type a command..."};

        // @desc Show the vertical scrollbar in the picker
        // @type bool
        // @default true
        bool vscrollbar{true};

        // @desc Show shortcuts
        // @type bool
        // @default true
        bool shortcuts{true};

        // @desc Show grids
        // @type bool
        // @default false
        bool grid{false};
    } command_palette{};
    // @endsection

    // @section Picker
    // @section_desc Split options struct
    // @section_type struct
    struct Picker
    {

        // @desc Show stylish border around picker
        // @type bool
        // @default true
        bool border{true};

        // @section Shadow
        // @section_desc Picker shadow struct
        // @section_type struct
        struct shadow
        {
            // @desc Enable shadow
            // @type bool
            // @default true
            bool enabled{true};

            // @desc Blur radius of shadow in pixels
            // @type int
            // @default 18
            int blur_radius{18};

            // @desc X-offset of the shadow
            // @type int
            // @default 0
            int offset_x{0};

            // @desc Y-offset of the shadow
            // @type int
            // @default 6
            int offset_y{6};

            // @desc Opacity of the shadow
            // @type int
            // @default 120
            int opacity{120};
        } shadow{};
    } picker{};
    // @endsection

#ifdef ENABLE_LLM_SUPPORT
    // @section LLM Widget
    // @section_desc LLM widget options struct
    // @section_type struct
    struct Llm_widget
    {
        // @desc Show the LLM Widget
        // @type bool
        // @default false
        bool visible{false};

        // @desc LLM Widget Panel position
        // @type str
        // @choice left, right, top, bottom
        // @default right
        QString panel_position{"right"};

        // @desc LLM Widget Panel width
        // @type int
        // @default 400
        int panel_width{400};
    } llm_widget{};
    // @endsection

    // @section LLM
    // @section_desc LLM options struct
    // @section_type struct
    struct LLM
    {
        // @desc LLM Provider
        // @type str
        // @default ollama
        std::string provider{"ollama"};

        // @desc LLM Model to use
        // @type str
        // @default "llama2-7b-chat"
        std::string model{"llama2-7b-chat"};

        // @desc Max tokens
        // @type int
        // @default 512
        int max_tokens{512};
        // float temperature{0.7f}; //
    } llm{};
// @endsection
#endif

    // @section Rendering
    // @section_desc Rendering options struct
    // @section_type struct
    struct Rendering
    {
        using DPR = std::variant<float, QMap<QString, float>>;
        // @desc Device Pixel Ratio
        // @type float or table
        // @default 1.0
        DPR dpr{1.0f};

        // @desc Enable Antialiasing
        // @type bool
        // @default true
        bool antialiasing{true};

        // @desc Antialiasing bits
        // @type int
        // @default 8
        int antialiasing_bits{8};

        // @desc Enable text antialiasing
        // @type bool
        // @default true
        bool text_antialiasing{true};

        // @desc Smooth pixmap transform
        // @type bool
        // @default true
        bool smooth_pixmap_transform{true};

    } rendering{};
    // @endsection

    // @section Behavior
    // @section_desc Behavior options struct
    // @section_type struct
    struct Behavior
    {
        // @desc Confirm before quitting
        // @type bool
        // @default true
        bool confirm_on_quit{true};

        // @desc Undo limit
        // @type int
        // @default 25
        int undo_limit{25};

        // @desc Number of pages to cache page
        // @type int
        // @default 10
        int cache_pages{10};

        // @desc Preload pages
        // @type int
        // @default 2
        int preload_pages{2};

        // @desc Auto-reload file upon detecting file change
        // @type bool
        // @default true
        bool auto_reload{true};

        // @desc Invert color mode (aka dark mode)
        // @type bool
        // @default false
        bool invert_mode{false};

        // @desc Open last visited file when launching new instance
        // @type bool
        // @default false
        bool open_last_visited{false};

        // @desc Always open files in new window
        // @type bool
        // @default false
        bool always_open_in_new_window{false};

        // @desc Remembers last visited page number
        // @type bool
        // @default true
        bool remember_last_visited{true};

        // @desc Keep track of recent files
        // @type bool
        // @default true
        bool recent_files{true};

        // @desc Max number of recent files to keep track
        // @type int
        // @default 10
        int num_recent_files{10};

        // @desc Page history location limit to keep track
        // @type int
        // @default 5
        int page_history_limit{5};

        // Don't document this as it's for internal use
        int startpage_override{-1};

        // @desc Initial interaction mode
        // @type str
        // @choice {
        // text_selection, region_selection, annot_rect, annot_popup,
        // annot_highlight
        // }
        // @default text_selection
        GraphicsView::Mode initial_mode{GraphicsView::Mode::TextSelection};
#ifdef HAS_SYNCTEX

        // @desc Synctex editor command
        // @type str
        // @default ""
        QString synctex_editor_command{QString()};
#endif
    } behavior{};
    // @endsection
};

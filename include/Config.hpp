#pragma once

#include "DocumentView.hpp"
#include "GraphicsView.hpp"

#include <QColor>
#include <QHash>
#include <Qt>
#include <array>

struct Config
{
    struct MouseBinding
    {
        Qt::MouseButton button           = Qt::NoButton;
        Qt::KeyboardModifiers modifiers  = Qt::NoModifier;
        GraphicsView::MouseAction action = GraphicsView::MouseAction::None;

        inline bool isValid() const noexcept
        {
            return button != Qt::NoButton;
        }
    };

    QHash<QString, QString> keybinds;
    std::vector<MouseBinding> mousebinds{
        {Qt::LeftButton, Qt::ShiftModifier,
         GraphicsView::MouseAction::SynctexJump},
        {Qt::LeftButton, Qt::ControlModifier,
         GraphicsView::MouseAction::Portal},
    };

    // @section page
    // @section_desc Page options struct
    // @section_type struct
    // @section_added 0.6.4
    struct Page
    {
        // @desc Page background color
        // @type str
        // @default "#FFFFFFFF"
        // @added 0.6.4
        uint32_t bg = 0xFFFFFFFF;

        // @desc Page foreground color
        // @type str
        // @default "#000000FF"
        // @added 0.6.4
        uint32_t fg = 0x000000FF;
    } page;
    // @endsection

    // @section synctex
    // @section_desc Synctex options struct
    // @section_type struct
    // @section_added 0.6.9
    struct Synctex
    {
        // @desc Enable synctex support
        // @type bool
        // @default true
        // @added 0.6.9
        bool enabled = true;

        // @desc Synctex editor command
        // @type str
        // @default ""
        // @added 0.3.0
        QString editor_command = QString();
    } synctex;

    // @section search
    // @section_desc Search options struct
    // @section_type struct
    // @section_added 0.6.0
    struct Search
    {

        // @desc Search match count color
        // @type str
        // @default "#55500033"
        // @added 0.6.5
        uint32_t match_color = 0x55500033;

        // @desc Search match index color
        // @type str
        // @default "#55FF0055"
        // @added 0.6.5
        uint32_t index_color = 0x55FF0055;

        // @desc {
        // Progressive search (renders results progressively when enabled,
        // which can speed up search on large documents)
        // }
        // @type bool
        // @default true
        // @added 0.6.8
        bool progressive = true;

        // @desc {
        // Highlight all search matches (if false, only the current match
        // is highlighted)
        // }
        // @type bool
        // @default true
        // @added 0.6.8
        bool highlight_matches = true;

        // @desc {
        // When true, the "next" match is always the next index in the results
        // list. When false, the "next" match is the one physically closest to
        // the cursor.
        // }
        // @type bool
        // @default false
        // @added 0.6.9
        bool absolute_jump = false;

    } search;
    // @endsection

    // @section annotations
    // @section_desc Annotation options struct
    // @section_type struct
    // @section_added 0.6.5
    struct Annotations
    {
        // @section Annotations.[Type]
        // @section_desc {
        // Base annotation options struct (common to all annotation types)
        // }
        // @section_type struct
        // @section_added 0.6.5
        struct Base
        {
            // @desc Enable hover effect (e.g., glow on highlight)
            // @type bool
            // @default true
            // @added 0.6.5
            bool hover_glow = true;

            // @desc Annotation glow color (on hover)
            // @type str
            // @default "#FF5000AA"
            // @added 0.6.5
            uint32_t glow_color = 0xFF5000AA;

            // @desc Glow width in pixels
            // @type int
            // @default 5
            // @added 0.6.5
            int glow_width = 5;

            // @desc { Show comment text in tooltip on hover (if comment
            // exists) }
            // @type bool
            // @default true
            // @added 0.6.5
            bool comment = true;

            // @desc Font size for the comment tooltip (in points)
            // @type int
            // @default 12
            // @added 0.6.5
            int comment_font_size = 12;
        };
        // @endsection

        // @section annotations.popup
        // @section_desc Popup annotation options struct
        // @section_type struct
        // @section_added 0.6.5
        struct Popup : public Base
        {
        } popup;
        // @endsection

        // @section Annotations.Rect
        // @section_desc Rect annotation options struct
        // @section_type struct
        // @section_added 0.6.5
        struct Rect : public Base
        {
            // @desc Show marker in the corner of the rect (if comment exists)
            // @type bool
            // @default true
            // @added 0.6.5
            bool comment_marker = true;

            // @desc Fill color for rect annotations
            // @type str
            // @default "#55FF5588"
            // @added 0.6.5
            uint32_t color = 0x55FF5588;
        } rect;
        // @endsection

        // @section annotations.highlight
        // @section_desc Highlight annotation options struct
        // @section_type struct
        // @section_added 0.6.5
        struct Highlight : public Base
        {
            // @desc Show marker in the corner of the rect (if comment exists)
            // @type bool
            // @default true
            // @added 0.6.5
            bool comment_marker = true;

            // @desc Fill color for rect annotations
            // @type str
            // @default "#55FF5588"
            // @added 0.6.5
            uint32_t color = 0x55FF5588;
        } highlight;
        // @endsection

    } annotations;
    // @endsection

    // @section thumbnail_panel
    // @section_desc Thumbnail panel options struct
    // @section_type struct
    // @section_added 0.6.9
    struct ThumbnailPanel
    {
        // @desc Show page number below page thumbnail
        // @type bool
        // @default true
        // @added 0.6.9
        bool show_page_numbers = true;

        // @desc Relative width of the thumbnail panel (compared to the main
        // view)
        // @type float
        // @default 0.15
        // @added 0.6.9
        float panel_width = 0.15;
    } thumbnail;
    // @endsection

    // @section portal
    // @section_desc Portal options struct
    // @section_type struct
    // @section_added 0.6.4
    struct Portal
    {
        // @desc Portal border color
        // @type str
        // @default "#FFFFFFAA"
        // @added 0.6.4
        uint32_t border_color = 0xFFFFFFAA;

        // @desc Enable portal ability
        // @type bool
        // @default true
        // @added 0.6.4
        bool enabled = true;

        // @desc Portal border width
        // @type int
        // @default 5
        // @added 0.6.4
        int border_width = 5;

        // @desc {
        // Respect parent's destruction (if parent closes, close the
        // portal too)
        // }
        // @type bool
        // @default true
        // @added 0.6.4
        bool respect_parent = true;

        // @desc Dim inactive for portal views too
        // @type bool
        // @default false
        // @added 0.6.5
        bool dim_inactive = false;
    } portal;
    // @endsection

    // @section window
    // @section_desc Window options struct
    // @section_type struct
    // @section_added 0.3.0
    struct Window
    {
        // @desc Background color
        // @type str
        // @default "#00000000"
        // @added 0.3.0
        uint32_t bg = 0x00000000;

        // @desc Accent color
        // @type str
        // @default "#3daee9FF"
        // @added 0.3.0
        uint32_t accent = 0x3daee9FF;

        // @desc Set the window fullscreen
        // @type bool
        // @default false
        // @added 0.3.0
        bool fullscreen = false;

        // @desc Show the menubar
        // @type bool
        // @default true
        // @added 0.3.0
        bool menubar = true;

        // @desc Show startup widget tab
        // @type bool
        // @default true
        // @added 0.3.0
        bool startup_tab = false;

        // @desc Title format for the window title
        // @type str
        // @default "{} - lektra"
        // @added 0.3.0
        QString title_format = "{} - lektra";

        // Required for documentation parsing, do not remove
        using WindowSize = std::array<int, 2>;

        // @desc Initial size of the window
        // @type table
        // @default 600,400
        // @added 0.6.3
        WindowSize initial_size = {600, 400}; // width, height; -1 for default
    } window;
    // @endsection

    // @section layout
    // @section_desc Layout options struct
    // @section_type struct
    // @section_added 0.5.1
    struct Layout
    {

        // @desc Initial page layout mode
        // @type str
        // @choice vertical, horizontal, single, book
        // @default vertical
        // @added 0.5.1
        DocumentView::LayoutMode mode = DocumentView::LayoutMode::VERTICAL;

        // @desc Initial page fit mode
        // @default width
        // @choice width, height, window
        // @added 0.5.1
        DocumentView::FitMode initial_fit = DocumentView::FitMode::Width;

        // @desc Apply auto fit when resizing window
        // @type bool
        // @default false
        // @added 0.5.1
        bool auto_resize = false;

        // @desc Page spacing in pixels
        // @type int
        // @default 10
        // @added 0.5.1
        int spacing = 10;
    } layout;
    // @endsection

    // @section statusbar
    // @section_desc Statusbar options struct
    // @section_type struct
    // @section_added 0.5.5
    struct Statusbar
    {
        // @desc Show statusbar
        // @type bool
        // @default true
        // @added 0.5.5
        bool visible = true;

        using Padding   = std::array<int, 4>;
        // @desc Padding
        // @type table
        // @note { Order is left, top, right, down }
        // @default [2, 2, 2, 2]
        // @added 0.5.5
        Padding padding = {2, 2, 2, 2};

        // @section statusbar.component
        // @section_desc {
        // Statusbar component options struct. These are individual component
        // in the statusbar that can be toggled on/off independently of the
        // entire statusbar visibility.
        // }
        // @section_type struct
        // @section_added 0.7.0
        struct component
        {
            struct Session
            {
                // @desc Show session name in the statusbar (if in session)
                // @type bool
                // @default true
                // @added 0.7.0
                bool show = true;
            } session;

            // @section statusbar.component.filename
            // @section_desc File name component options struct
            // @section_type struct
            // @section_added 0.7.0
            struct FileName
            {
                // @desc Show file info
                // @type bool
                // @default true
                // @added 0.7.0
                bool show = true;

                // @desc Show full path
                // @type bool
                // @default true
                // @added 0.7.0
                bool full_path = true;
            } filename;
            // @endsection

            // @section statusbar.component.pagenumber
            // @section_desc Page number component options struct
            // @section_type struct
            // @section_added 0.7.0
            struct PageNumber
            {
                // @desc Show page number
                // @type bool
                // @default true
                // @added 0.7.0
                bool show = true;

            } pagenumber;
            // @endsection

            // @section statusbar.component.zoom
            // @section_desc Zoom component options struct
            // @section_type struct
            // @section_added 0.7.0
            struct Zoom
            {
                // @desc Show page number
                // @type bool
                // @default true
                // @added 0.7.0
                bool show = true;

            } zoom;
            // @endsection

            // @section statusbar.component.progress
            // @section_desc Page progress component options struct
            // @section_type struct
            // @section_added 0.7.0
            struct Progress
            {
                // @desc Show page progress
                // @type bool
                // @default true
                // @added 0.7.0
                bool show = true;

            } progress;
            // @endsection

            // @section statusbar.component.mode
            // @section_desc Interaction mode options struct
            // @section_type struct
            // @section_added 0.7.0
            struct Mode
            {
                // @desc Show interaction mode in the statusbar
                // @type bool
                // @default true
                // @added 0.7.0
                bool show = true;

                // @desc Show interaction mode in text (e.g., "Select", "Pan")
                // @type bool
                // @default true
                // @added 0.7.0
                bool text = true;

                // @desc Show interaction mode as an icon
                // @type bool
                // @default false
                // @added 0.7.0
                bool icon = true;
            } mode;

        } component;

    } statusbar;
    // @endsection

    // @section zoom
    // @section_desc Zoom options struct
    // @section_type struct
    // @section_added 0.3.0
    struct Zoom
    {

        // @desc Default zoom level
        // @type float
        // @default 0.5f
        // @added 0.3.0
        float level = 0.5f;

        // @desc Zoom factor
        // @type float
        // @default 1.25f
        // @added 0.3.0
        float factor = 1.25f;

        // @desc Anchor zoom to mouse position (instead of center of the view)
        // @type bool
        // @default true
        // @added 0.6.7
        bool anchor_to_mouse = true;

    } zoom;
    // @endsection

    // @section selection
    // @section_desc Selection options struct
    // @section_type struct
    // @section_added 0.5.1
    struct Selection
    {
        // @desc Threshold in pixels before actually starting the selection
        // @type int
        // @default 50
        // @added 0.5.1
        int drag_threshold = 50;

        // @desc Copy on text selection
        // @type bool
        // @default false
        // @added 0.6.3
        bool copy_on_select = false;

        // @desc Selection color
        // @type str
        // @default "#0000FF55"
        // @added 0.5.1
        uint32_t color = 0x0000FF55;

    } selection;
    // @endsection

    // @section split
    // @section_desc Split options struct
    // @section_type struct
    // @section_added 0.6.2
    struct Split
    {
        // @desc Focus of split follows the mouse
        // @type bool
        // @default true
        // @added 0.6.2
        bool focus_follows_mouse = true;

        // @desc Moving mouse changes the focus
        // @type bool
        // @default true
        // @added 0.6.2
        bool mouse_follows_focus = true;

        // @desc Dims splits that are not currently focused
        // @type bool
        // @default false
        // @added 0.6.2
        bool dim_inactive = false;

        // @desc Set the inactive split dim opacity [0-1]
        // @type float
        // @default 0.5f
        // @added 0.6.2
        float dim_inactive_opacity = 0.5f; // 0.0 (no dim) to 1.0 (fully dimmed)
    } split;
    // @endsection

    // @section scrollbars
    // @section_desc Scrollbars options struct
    // @section_type struct
    // @section_added 0.5.1
    struct Scrollbars
    {
        // @desc Show the horizontal scrollbar
        // @type bool
        // @default true
        // @added 0.5.4
        bool horizontal = true;

        // @desc Show the vertical scrollbar
        // @type bool
        // @default true
        // @added 0.5.4
        bool vertical = true;

        // @desc Show the search hits in scrollbar
        // @type bool
        // @default true
        // @added 0.5.1
        bool search_hits = true;

        // @desc Auto hide after timeout
        // @type bool
        // @default true
        // @added 0.5.4
        bool auto_hide = true;

        // @desc Size of the scrollbar in pixels
        // @type int
        // @default 12
        // @added 0.5.4
        int size = 12;

        // @desc Inactive timeout after which to hide the scrollbar
        // @type float
        // @default 1.5
        // @added 0.5.4
        float hide_timeout = 1.5; // seconds of inactivity before hiding
    } scrollbars;
    // @endsection

    // @section jump_marker
    // @section_desc Jump marker options struct
    // @section_type struct
    // @section_added 0.3.1
    struct JumpMarker
    {
        // @desc Show the jump marker
        // @type bool
        // @default true
        // @added 0.3.1
        bool enabled = true;

        // @desc Jump marker color
        // @type str
        // @default "#FF0000FF"
        // @added 0.3.1
        uint32_t color = 0xFF0000FF;

        // @desc Jump marker fade duration in seconds
        // @type float
        // @default 1.0
        // @added 0.6.7
        float fade_duration = 1.0f;

    } jump_marker;
    // @endsection

    // @section links
    // @section_desc Links options struct
    // @section_type struct
    // @section_added 0.3.1
    struct Links
    {
        // @desc Enable link support (both pdf links and detected URLs)
        // @type bool
        // @default true
        // @added 0.6.5
        bool enabled = true;

        // @desc Show the rect boundary for links
        // @type bool
        // @default false
        // @added 0.3.1
        bool boundary = false;

        // @desc Detect non-pdf link objects that are valid links
        // @type bool
        // @default false
        // @added 0.5.3
        bool detect_urls = false;

        // @desc Valid regular expression that detects URLs
        // @type str
        // @default R"((https?://|www\.)[^\s<>()\"']+)"
        // @added 0.5.3
        QString url_regex = R"((https?://|www\.)[^\s<>()\"']+)";
    } links;
    // @endsection

    // @section link_hints
    // @section_desc Link hint options struct
    // @section_type struct
    // @section_added 0.3.0
    struct Link_hints
    {
        // @desc Size of the link hint rects
        // @type float
        // @default 0.5f
        // @added 0.3.0
        float size = 0.5f;

        // @desc Link hint background color
        // @type str
        // @default "#000000FF"
        // @added 0.6.4
        uint32_t bg = 0x000000FF;

        // @desc Link hint foreground color
        // @type str
        // @default "#ea3ee9FF"
        // @added 0.6.4
        uint32_t fg = 0xea3ee9FF;

    } link_hints;
    // @endsection

    // @section tabs
    // @section_desc Tab options struct
    // @section_type struct
    // @section_added 0.3.0
    struct Tabs
    {
        // @desc Show tabs
        // @type bool
        // @default true
        // @added 0.3.0
        bool visible = true;

        // @desc Auto hide tabs when tab count equals 1
        // @type bool
        // @default false
        // @added 0.3.0
        bool auto_hide = false;

        // @desc Show the tab close buttons
        // @type bool
        // @default true
        // @added 0.3.0
        bool closable = true;

        // @desc Tabs can be rearranged
        // @type bool
        // @default true
        // @added 0.3.0
        bool movable = true;

        // @desc Text elide mode when text can't fit in the tab fully
        // @type str
        // @choice right, left, middle, none
        // @default right
        // @added 0.3.0
        Qt::TextElideMode elide_mode = Qt::TextElideMode::ElideRight;

        // @desc Location of the tabs in the window
        // @type str
        // @choice top, bottom, left, right
        // @default top
        // @added 0.3.0
        QTabWidget::TabPosition location = QTabWidget::TabPosition::North;

        // @desc Show full file path
        // @type bool
        // @default false
        // @added 0.3.0
        bool full_path = false;

        // @desc Lazy load tabs
        // @type bool
        // @default true
        // @added 0.6.0
        bool lazy_load = true;
    } tabs;
    // @endsection

    // @section picker
    // @section_desc Picker options struct
    // @section_type struct
    // @section_added 0.6.0
    struct Picker
    {
        // @desc {
        // Width of picker in pixels (if greater than 1) or relative
        // to the window width (0.0 - 1.0, if float)
        // }
        // @type int
        // @default 600
        // @added 0.6.9
        float width = 600;

        // @desc {
        // Height of picker in pixels (if greater than 1) or relative
        // to the window height (0.0 - 1.0, if float)
        // }
        // @type int
        // @default 400
        // @added 0.6.9
        float height = 400;

        // @desc Show stylish border around picker
        // @type bool
        // @default true
        // @added 0.6.0
        bool border = true;

        // @desc Enable alternating row colors in the picker list
        // @type bool
        // @default true
        // @added 0.6.8
        bool alternating_row_color = true;

        // clang-format off
        // @section Picker.Keys
        // @section_type struct
        // @section_added 0.6.0
        // @section_desc {
        //   <p>Picker keyboard binding options struct.</p>
        // <p> Valid actions are:</p>
        // <ul>
        // <li><code class="inline">up</code></li>
        // <li><code class="inline">down</code></li>
        // <li><code class="inline">select</code></li>
        // <li><code class="inline">dismiss</code></li>
        // </ul>
        //   <p>Example:</p>
        //   <pre><code class="language-toml">[picker.keys]<br>
        //   # Format: command_name = "KeyCombination"
        //   # Single key
        //   up = "Up"
        //   # Multiple keys
        //   down = ["j", "Down"] # Added in 0.6.9
        //   </code></pre>
        //
        // }
        //
        // @desc up
        // @type str
        //
        // @endsection
        // clang-format on

        // @section picker.shadow
        // @section_desc Picker shadow struct
        // @section_type struct
        // @section_added 0.6.0
        struct shadow
        {
            // @desc Enable shadow
            // @type bool
            // @default true
            // @added 0.6.0
            bool enabled = true;

            // @desc Blur radius of shadow in pixels
            // @type int
            // @default 18
            // @added 0.6.0
            int blur_radius = 18;

            // @desc X-offset of the shadow
            // @type int
            // @default 0
            // @added 0.6.0
            int offset_x = 0;

            // @desc Y-offset of the shadow
            // @type int
            // @default 6
            // @added 0.6.0
            int offset_y = 6;

            // @desc Opacity of the shadow
            // @type int
            // @default 120
            // @added 0.6.0
            int opacity = 120;
        } shadow;
    } picker;
    // @endsection

    // @section outline
    // @section_desc Outline options struct
    // @section_type struct
    // @section_added 0.5.3
    struct Outline : public Picker
    {
        // @desc Indent width
        // @type int
        // @default 10
        // @added 0.6.5
        int indent_width = 10;

        // @desc Show page numbers
        // @type bool
        // @default true
        // @added 0.6.5
        bool show_page_number = true;

        // @desc {
        // if true, show the outline in a flat menu instead of a hierarchical
        // one
        // }
        // @type bool
        // @default true
        // @added 0.6.9
        bool flat_menu = true;

    } outline;
    // @endsection

    // @section highlight_search
    // @section_desc Highlight Search options struct
    // @section_type struct
    // @section_added 0.5.3
    struct HighlightSearch : public Picker
    {
        // @desc {
        // if true, show the outline in a flat menu instead of a hierarchical
        // one
        // }
        // @type bool
        // @default true
        // @added 0.6.9
        bool flat_menu = true;
    } highlight_search;
    // @endsection

    // @section command_palette
    // @section_desc Command Palette options struct
    // @section_type struct
    // @section_added 0.5.4
    struct CommandPalette : public Picker
    {
        // @desc Placeholder text for the input field
        // @type str
        // @default "Type a command..."
        // @added 0.5.4
        QString placeholder_text = "Type a command...";

        // @desc Show the vertical scrollbar in the picker
        // @type bool
        // @default true
        // @added 0.5.4
        bool vscrollbar = true;

        // @desc Show shortcuts
        // @type bool
        // @default true
        // @added 0.5.4
        bool show_shortcuts = true;

        // @desc Show command description
        // @type bool
        // @default false
        // @added 0.6.5
        bool description = false;
    } command_palette;
    // @endsection

#ifdef ENABLE_LLM_SUPPORT
    // @section llm_widget
    // @section_desc LLM widget options struct
    // @section_type struct
    // @section_note {
    // This is still a work in progress and subject to change, but
    // the options are here for documentation purposes
    // }
    // @section_added 0.5.4
    struct Llm_widget
    {
        // @desc Show the LLM Widget
        // @type bool
        // @default false
        // @added 0.5.4
        bool visible = false;

        // @desc LLM Widget Panel position
        // @type str
        // @choice left, right, top, bottom
        // @default right
        // @added 0.5.4
        QString panel_position = "right";

        // @desc LLM Widget Panel width
        // @type int
        // @default 400
        // @added 0.5.4
        int panel_width = 400;
    } llm_widget;
    // @endsection

    // @section llm
    // @section_desc LLM options struct
    // @section_type struct
    // @section_added 0.5.4
    // @section_note {
    // This is still a work in progress and subject to change, but
    // the options are here for documentation purposes
    // }
    struct LLM
    {
        // @desc LLM Provider
        // @type str
        // @default ollama
        // @added 0.5.4
        std::string provider = "ollama";

        // @desc LLM Model to use
        // @type str
        // @default "llama2-7b-chat"
        // @added 0.5.4
        std::string model = "llama2-7b-chat";

        // @desc Max tokens
        // @type int
        // @default 512
        // @added 0.5.4
        int max_tokens = 512;
        // float temperature= 0.7f; //
    } llm;
// @endsection
#endif

    // @section rendering
    // @section_desc Rendering options struct
    // @section_type struct
    // @section_added 0.3.0
    struct Rendering
    {
        using DPR = std::variant<float, QMap<QString, float>>;
        // @desc Device Pixel Ratio
        // @type float or table
        // @default 1.0
        // @added 0.3.0
        DPR dpr   = 1.0f;

        // @desc Enable Antialiasing
        // @type bool
        // @default true
        // @added 0.6.3
        bool antialiasing = true;

        // @desc Antialiasing bits
        // @type int
        // @default 8
        // @added 0.3.0
        int antialiasing_bits = 8;

        // @desc Enable text antialiasing
        // @type bool
        // @default true
        // @added 0.6.3
        bool text_antialiasing = true;

        // @desc Smooth pixmap transform
        // @type bool
        // @default true
        // @added 0.6.3
        bool smooth_pixmap_transform = true;

        enum class Backend
        {
            Auto,   // Automatically choose the best available backend
            Raster, // Use raster rendering (CPU-based)
            OpenGL  // Use OpenGL rendering (GPU-accelerated)
        };

        // @desc Rendering backend to use
        // @type str
        // @choice auto, raster, opengl
        // @added 0.6.6
        // @default auto
        Backend backend = Backend::Raster;

    } rendering;
    // @endsection

    // @section behavior
    // @section_desc Behavior options struct
    // @section_type struct
    // @section_added 0.3.0
    struct Behavior
    {
        // @desc Confirm before quitting
        // @type bool
        // @default false
        // @added 0.3.0
        bool confirm_on_quit = false;

        // @desc Undo limit
        // @type int
        // @default 25
        // @added 0.3.1
        int undo_limit = 25;

        // @desc Number of pages to cache page
        // @type int
        // @default 10
        // @added 0.5.3
        int cache_pages = 10;

        // @desc Preload pages
        // @type int
        // @default 2
        // @added 0.6.3
        int preload_pages = 2;

        // @desc Auto-reload file upon detecting file change
        // @type bool
        // @default true
        // @added 0.3.0
        bool auto_reload = true;

        // @desc Invert color mode (aka dark mode)
        // @type bool
        // @default false
        // @added 0.3.0
        bool invert_mode = false;

        // @desc {
        // Preserve images when in invert color mode. When enabled, images in
        // the PDF will retain their original colors even when invert mode is
        // active. This prevents photos and diagrams from appearing inverted.
        // }
        // @type bool
        // @default true
        // @added 0.6.6
        bool dont_invert_images = true;

        // @desc Open last visited file when launching new instance
        // @type bool
        // @default false
        // @added 0.3.0
        bool open_last_visited = false;

        // @desc Always open files in new window
        // @type bool
        // @default false
        // @added 0.5.3
        bool always_open_in_new_window = false;

        // @desc Remembers last visited page number
        // @type bool
        // @default true
        // @added 0.3.0
        bool remember_last_visited = true;

        // @desc Keep track of recent files
        // @type bool
        // @default true
        // @added 0.3.0
        bool recent_files = true;

        // @desc Max number of recent files to keep track
        // @type int
        // @default 10
        // @added 0.3.0
        int num_recent_files = 10;

        // @desc Page history location limit to keep track
        // @type int
        // @default 5
        // @added 0.3.0
        int page_history_limit = 5;

        // Don't document this as it's for internal use
        int _startpage_override{-1};

        // @desc Initial interaction mode
        // @type str
        // @choice {
        // text_selection, region_selection, annot_rect, annot_popup,
        // annot_highlight
        // }
        // @default text_selection
        // @added 0.3.0
        GraphicsView::Mode initial_mode = GraphicsView::Mode::TextSelection;
    } behavior;
    // @endsection

    // Just for documentation parsing, do not remove

    // clang-format off
    // @section mousebindings
    // @section_type struct
    // @section_added 0.6.6
    // @section_desc {
    // Mouse binding options struct.
    //
    // <p>Valid commands are:
    //
    // <ul>
    // <li><code class="inline">preview</code> - Shows a preview of the link location in a floating window</li>
    // <li><code class="inline">synctex_jump</code> - Takes to the source code if opened in SyncTeX mode</li>
    // <li><code class="inline">portal</code> - Opens a portal pointing to the clicked link</li>
    // </ul>
    //
    // <p><b>Example</b>:</p>
    // <pre><code class="language-toml">[mousebindings]
    // # Format: mouse_command_name = "Modifier(s)+MouseButton"
    // synctex_jump = "Shift+LeftButton"
    //
    // </code></pre>
    // }
    // @endsection
    // clang-format on

    // clang-format off
    // @section keybindings
    // @section_type struct
    // @section_added 0.3.0
    // @section_desc {
    //   <p>Keyboard binding options struct.</p>
    //
    //   <p>By default, Lektra loads the default keybindings. You can disable this
    //   by setting <code class="inline">load_defaults</code> to
    //   <code class="inline">false</code> in the config file.</p>
    //
    //   <p>Example:</p>
    //   <pre><code class="language-toml">[keybindings]<br>
    //   # Format: command_name = "KeyCombination"
    //   # Single key
    //   open_outline = "Ctrl+O"
    //   # Multiple keys
    //   scroll_down = ["j", "Down"] # Added in 0.6.9
    //   </code></pre>
    //
    //   Commands can be viewed via:
    //   <ul>
    //     <li>The <code class="inline">--list-commands</code> command line option</li>
    //     <li>The <a href="https://dheerajshenoy.github.io/lektra/commands">Commands Reference</a></li>
    //     <li>The command palette inside Lektra</li>
    //   </ul>
    // }
    // @endsection
    // clang-format on

    // @section preview
    // @section_desc Preview options struct
    // @section_type struct
    // @section_added 0.6.6
    struct Preview
    {

        // Required for documentation parsing, do not remove
        using WindowRatio = std::array<float, 2>;

        // @desc {
        // Size of the preview window as a ratio of the main window size
        // (width, height)
        // }
        // @type table
        // @default { width = 0.6, height = 0.7 }
        // @added 0.6.6
        WindowRatio size_ratio = {0.6, 0.7};

        // @desc Border radius of the preview window in pixels
        // @type int
        // @default 8
        // @added 0.6.6
        int border_radius = 8;

        // @desc Close the preview window when clicking outside of it
        // @type bool
        // @default true
        // @added 0.6.6
        bool close_on_click_outside = true;

        // @desc Opacity of the preview window (0.0 to 1.0)
        // @type float
        // @default 0.95
        // @added 0.6.6
        float opacity = 0.95f;

    } preview;
    // @endsection

    // @section misc
    // @section_desc Misc options struct (for options that don't fit in other
    // categories)
    // @section_type struct
    // @section_added 0.7.0
    struct Misc
    {
        // @desc {
        // Colors shown in the color dialog
        // }
        // @type list[str]
        // @default {
        // ["#FFFF0080", "#FFA50080", "#FF000080", "#00FF0080",
        // "#00C8FF80", "#8000FF80", "#FF69B480", "#0000FF80", "#FFFFFF80"]
        // }
        // @added 0.7.0
        std::vector<QColor> color_dialog_colors = {
            {255, 255, 0, 128},   // Yellow
            {255, 165, 0, 128},   // Orange
            {255, 0, 0, 128},     // Red
            {0, 255, 0, 128},     // Green
            {0, 200, 255, 128},   // Cyan
            {128, 0, 255, 128},   // Purple
            {255, 105, 180, 128}, // Pink
            {0, 0, 255, 128},     // Blue
            {255, 255, 255, 128}, // White
        };
    } misc;
    // @endsection
};

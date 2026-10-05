#include "AboutDialog.hpp"
#include "AppPaths.hpp"
#include "DispatchType.hpp"
#include "DocumentContainer.hpp"
#include "DocumentView.hpp"
#include "DonateDialog.hpp"
#include "EditLastPagesWidget.hpp"
#include "GraphicsView.hpp"
#include "Lektra.hpp"
#include "PageLocation.hpp"
#include "SaveSessionDialog.hpp"
#include "SearchBar.hpp"
#include "StartupWidget.hpp"
#include "StatusbarLayoutSpec.hpp"
#include "TabBar.hpp"
#include "toml.hpp"
#include "utils.hpp"

#include <QColorDialog>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QObject>
#include <QProcess>
#include <QSplitter>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWindow>
#include <variant>
namespace
{

static inline void
set_title_format_if_present(toml::node_view<toml::node> n,
                            QString &title_format)
{
    if (auto v = n.value<std::string>())
    {
        QString window_title = QString::fromStdString(*v);
        window_title         = window_title.replace("{}", "%1");
        title_format         = window_title;
    }
}

// Problems found while reading the config file, shown together once it has
// been read. Values of the wrong type used to be skipped without a word.
static QStringList g_config_issues;

static void
configIssue(const toml::node *node, const QString &message)
{
    const int line = node ? static_cast<int>(node->source().begin.line) : 0;
    g_config_issues << (line > 0 ? QString("line %1: %2").arg(line).arg(message)
                                 : message);
}

// A TOML value as plain Qt data (lists, tables, text, numbers, booleans), for
// options whose shape is free, like [statusbar].layout.
static QVariant
tomlToVariant(const toml::node &node)
{
    if (auto v = node.value<std::string>())
        return QString::fromStdString(*v);
    if (auto v = node.value<bool>())
        return *v;
    if (node.is_integer())
        return static_cast<qlonglong>(*node.value<int64_t>());
    if (node.is_floating_point())
        return *node.value<double>();
    if (auto *array = node.as_array())
    {
        QVariantList list;
        for (const toml::node &item : *array)
            list << tomlToVariant(item);
        return list;
    }
    if (auto *table = node.as_table())
    {
        QVariantMap map;
        for (auto &&[key, value] : *table)
            map.insert(QString::fromUtf8(key.data(),
                                         static_cast<qsizetype>(key.length())),
                       tomlToVariant(value));
        return map;
    }
    return {};
}

template <typename T>
static inline QString
expectedType()
{
    if constexpr (std::is_same_v<T, bool>)
        return QObject::tr("true or false");
    else if constexpr (std::is_integral_v<T>)
        return QObject::tr("a whole number");
    else if constexpr (std::is_floating_point_v<T>)
        return QObject::tr("a number");
    else
        return QObject::tr("a different type of value");
}

template <typename T>
static inline void
set(toml::node_view<toml::node> node, T &target)
{
    if (auto v = node.value<T>())
        target = *v;
    else if (node)
        configIssue(node.node(),
                    QObject::tr("expected %1").arg(expectedType<T>()));
}

static inline void
set(toml::node_view<toml::node> n, QString &dst)
{
    if (auto v = n.value<std::string>())
        dst = QString::fromStdString(*v);
    else if (n)
        configIssue(n.node(), QObject::tr("expected text in quotes"));
}

static inline void
set_color(toml::node_view<toml::node> n, uint32_t &dst)
{
    if (auto s = n.value<std::string>())
    {
        uint32_t tmp = dst;
        if (parseHexColor(*s, tmp))
            dst = tmp;
        else
            configIssue(n.node(), QObject::tr("'%1' is not a color; use "
                                              "\"#RRGGBB\" or \"#RRGGBBAA\"")
                                      .arg(QString::fromStdString(*s)));
    }
    else if (n)
        configIssue(n.node(), QObject::tr("expected a color like \"#RRGGBB\""));
}

static inline void
set_picker_shared(toml::node_view<toml::node> picker, Config::Picker &target)
{
    set(picker["width"], target.width);
    set(picker["height"], target.height);
    set(picker["border"], target.border);
    set(picker["alternating_row_color"], target.alternating_row_color);

    if (auto picker_shadow = picker["shadow"])
    {
        set(picker_shadow["enabled"], target.shadow.enabled);
        set(picker_shadow["blur_radius"], target.shadow.blur_radius);
        set(picker_shadow["offset_x"], target.shadow.offset_x);
        set(picker_shadow["offset_y"], target.shadow.offset_y);
        set(picker_shadow["opacity"], target.shadow.opacity);
    }
}

template <typename T>
static inline void
inherit_picker_defaults(const Config::Picker &base, T &target)
{
    static_assert(std::is_base_of_v<Config::Picker, T>,
                  "Target must derive from Config::Picker");
    static_cast<Config::Picker &>(target) = base;
}
} // namespace

// Initialize the config related stuff
// The settings a view keeps its own copy of. Used for the global [section]
// tables and again for [filetype.<type>.section] overrides, so both accept
// exactly the same keys.
static void
applyViewToml(toml::table &toml, Config &cfg)
{
    if (auto page = toml["page"])
    {
        set_color(page["bg"], cfg.page.bg);
        set_color(page["fg"], cfg.page.fg);
    }

    if (auto annots = toml["annotations"])
    {

        if (auto highlight = annots["highlight"])
        {
            set_color(highlight["color"], cfg.annotations.highlight.color);
            set(highlight["hover_glow"], cfg.annotations.highlight.hover_glow);
            set(highlight["comment"], cfg.annotations.highlight.comment);
            set(highlight["comment_marker"],
                cfg.annotations.highlight.comment_marker);
            set(highlight["glow_width"], cfg.annotations.highlight.glow_width);
            set_color(highlight["glow_color"],
                      cfg.annotations.highlight.glow_color);
            set(highlight["comment_font_size"],
                cfg.annotations.highlight.comment_font_size);
        }

        if (auto rect = annots["rect"])
        {
            set_color(rect["color"], cfg.annotations.rect.color);
            set(rect["hover_glow"], cfg.annotations.rect.hover_glow);
            set(rect["comment"], cfg.annotations.rect.comment);
            set(rect["comment_marker"], cfg.annotations.rect.comment_marker);
            set(rect["glow_width"], cfg.annotations.rect.glow_width);
            set_color(rect["glow_color"], cfg.annotations.rect.glow_color);
            set(rect["comment_font_size"],
                cfg.annotations.rect.comment_font_size);
        }

        if (auto popup = annots["popup"])
        {
            set(popup["hover_glow"], cfg.annotations.popup.hover_glow);
            set(popup["comment"], cfg.annotations.popup.comment);
            set(popup["glow_width"], cfg.annotations.popup.glow_width);
            set_color(popup["glow_color"], cfg.annotations.popup.glow_color);
            set(popup["comment_font_size"],
                cfg.annotations.popup.comment_font_size);
        }
    }

    if (auto layout = toml["layout"])
    {
        if (auto str = layout["mode"])
        {
            DocumentView::LayoutMode mode;

            if (str == "vertical")
                mode = DocumentView::LayoutMode::VERTICAL;
            else if (str == "single")
                mode = DocumentView::LayoutMode::SINGLE;
            else if (str == "horizontal")
                mode = DocumentView::LayoutMode::HORIZONTAL;
            else if (str == "book")
                mode = DocumentView::LayoutMode::BOOK;
            else
                mode = DocumentView::LayoutMode::VERTICAL;

            cfg.layout.mode = mode;
        }
        if (auto str = layout["initial_fit"])
        {
            DocumentView::FitMode initial_fit;

            if (str == "width")
            {
                initial_fit = DocumentView::FitMode::Width;
            }
            else if (str == "height")
            {
                initial_fit = DocumentView::FitMode::Height;
            }
            else if (str == "window")
            {
                initial_fit = DocumentView::FitMode::Window;
            }
            else
            {
                initial_fit = DocumentView::FitMode::Width;
            }

            cfg.layout.initial_fit = initial_fit;
        }
        set(layout["auto_resize"], cfg.layout.auto_resize);
        set(layout["spacing"], cfg.layout.spacing);
    }

    if (auto zoom = toml["zoom"])
    {
        set(zoom["level"], cfg.zoom.level);
        set(zoom["factor"], cfg.zoom.factor);
        set(zoom["anchor_to_mouse"], cfg.zoom.anchor_to_mouse);
    }

    // Reflowable documents
    if (auto reflow = toml["reflow"])
    {
        set(reflow["font_family"], cfg.reflow.font_family);
        set(reflow["font_size"], cfg.reflow.font_size);
        set(reflow["line_spacing"], cfg.reflow.line_spacing);
    }

    if (!cfg.reflow.font_family.isEmpty())
        Model::prewarmFontIndex();

    if (auto selection = toml["selection"])
    {
        set(selection["drag_threshold"], cfg.selection.drag_threshold);
        set(selection["copy_on_select"], cfg.selection.copy_on_select);
        set_color(selection["color"], cfg.selection.color);
    }

    if (auto scrollbars = toml["scrollbars"])
    {
        set(scrollbars["vertical"], cfg.scrollbars.vertical);
        set(scrollbars["horizontal"], cfg.scrollbars.horizontal);
        set(scrollbars["search_hits"], cfg.scrollbars.search_hits);
        set(scrollbars["auto_hide"], cfg.scrollbars.auto_hide);
        set(scrollbars["size"], cfg.scrollbars.size);
        set(scrollbars["hide_timeout"], cfg.scrollbars.hide_timeout);
    }

    if (auto jump_marker = toml["jump_marker"])
    {
        set(jump_marker["enabled"], cfg.jump_marker.enabled);
        set_color(jump_marker["jump_marker"], cfg.jump_marker.color);
        set(jump_marker["fade_duration"], cfg.jump_marker.fade_duration);
    }

    if (auto links = toml["links"])
    {
        set(links["enabled"], cfg.links.enabled);
        set(links["boundary"], cfg.links.boundary);
        set(links["detect_urls"], cfg.links.detect_urls);
        set(links["url_regex"], cfg.links.url_regex);
        set(links["hover_preview"], cfg.links.hover_preview);
        set(links["hover_preview_delay"], cfg.links.hover_preview_delay);
        set(links["hover_preview_width"], cfg.links.hover_preview_width);
        set(links["hover_preview_height"], cfg.links.hover_preview_height);
    }

    if (auto search = toml["search"])
    {
        // set(search["case_sensitive"], cfg.search.case_sensitive);
        // set(search["whole_words"], cfg.search.whole_words);
        set(search["highlight_matches"], cfg.search.highlight_matches);
        set(search["progressive"], cfg.search.progressive);
        set(search["absolute_jump"], cfg.search.absolute_jump);
        set_color(search["match_color"], cfg.search.match_color);
        set_color(search["index_color"], cfg.search.index_color);
    }

    if (auto behavior = toml["behavior"])
    {
        set(behavior["preload_pages"], cfg.behavior.preload_pages);
        set(behavior["confirm_on_quit"], cfg.behavior.confirm_on_quit);
        set(behavior["undo_limit"], cfg.behavior.undo_limit);
        set(behavior["remember_last_visited"],
            cfg.behavior.remember_last_visited);
        set(behavior["single_instance"], cfg.behavior.single_instance);
        set(behavior["page_history"], cfg.behavior.page_history_limit);
        set(behavior["invert_mode"], cfg.behavior.invert_mode);
        set(behavior["dont_invert_images"], cfg.behavior.dont_invert_images);
        set(behavior["auto_reload"], cfg.behavior.auto_reload);
        set(behavior["cache_password"], cfg.behavior.cache_password);
        set(behavior["recent_files"], cfg.behavior.recent_files);
        set(behavior["num_recent_files"], cfg.behavior.num_recent_files);
        set(behavior["cache_pages"], cfg.behavior.cache_pages);
        set(behavior["mupdf_store_size"], cfg.behavior.mupdf_store_size);
        set(behavior["auto_scroll"], cfg.behavior.auto_scroll);
        set(behavior["close_on_last_tab"], cfg.behavior.close_on_last_tab);
        set(behavior["high_contrast"], cfg.behavior.high_contrast);
        set(behavior["high_contrast_black_point"],
            cfg.behavior.high_contrast_black_point);
        set(behavior["high_contrast_white_point"],
            cfg.behavior.high_contrast_white_point);
    }
}

// Convert a mouse binding string from the config file into a MouseBindKey
// struct
Config::MouseBinding
get_mouse_bind_key(const QString &trigger) noexcept
{
    Config::MouseBinding binding;

    const QStringList parts = trigger.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return binding;

    const QString key = parts.last().trimmed();

    // Determine trigger type
    if (key.compare("LeftButton", Qt::CaseInsensitive) == 0)
        binding.button = Qt::LeftButton;
    else if (key.compare("RightButton", Qt::CaseInsensitive) == 0)
        binding.button = Qt::RightButton;
    else if (key.compare("MiddleButton", Qt::CaseInsensitive) == 0)
        binding.button = Qt::MiddleButton;
    else
    {
        qWarning() << "Unknown mouse trigger:" << key;
        return binding;
    }

    // Parse modifiers
    for (int i = 0; i < parts.size() - 1; ++i)
    {
        const QString mod = parts[i].trimmed();
        if (mod.compare("Ctrl", Qt::CaseInsensitive) == 0)
            binding.modifiers |= Qt::ControlModifier;
        else if (mod.compare("Shift", Qt::CaseInsensitive) == 0)
            binding.modifiers |= Qt::ShiftModifier;
        else if (mod.compare("Alt", Qt::CaseInsensitive) == 0)
            binding.modifiers |= Qt::AltModifier;
        else if (mod.compare("Meta", Qt::CaseInsensitive) == 0)
            binding.modifiers |= Qt::MetaModifier;
        else
        {
            qWarning() << "Unknown modifier in mouse binding:" << mod;
            return Config::MouseBinding{};
        }
    }

    return binding;
}

void
Lektra::initConfig() noexcept
{

    auto primaryScreen                      = QGuiApplication::primaryScreen();
    m_screen_dpr_map[primaryScreen->name()] = primaryScreen->devicePixelRatio();

    m_app_data_dir = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    if (!m_app_data_dir.exists())
    {
        if (!m_app_data_dir.mkpath(".")) // Create the AppDataLocation directory
                                         // if it doesn't exist
        {
            qWarning() << "Failed to create data directory: "
                       << m_app_data_dir.absolutePath();
        }
    }

    m_session_dir         = m_app_data_dir.filePath("sessions");
    m_bookmarks_file_path = m_app_data_dir.filePath("bookmarks.json");

    if (!m_session_dir.exists())
    {
        if (!m_session_dir.mkpath("."))
        {
            qWarning() << "Failed to create sessions directory";
        }
    }

    if (m_skip_toml_config || !QFile::exists(m_config_file_path))
    {
        // No config file (or --config explicitly selected only an init.lua)
        // → apply compiled-in defaults and bail. Users who never wrote a
        // config still get all the standard keybindings.
        initDefaultKeybinds();
        return;
    }

    g_config_issues.clear();
    toml::table toml;

    try
    {
        toml = toml::parse_file(m_config_file_path.toStdString());
    }
    catch (const toml::parse_error &e)
    {
        const auto &begin = e.source().begin;
        QString text = tr("%1\n\nLine %2, column %3")
                           .arg(QString::fromUtf8(e.description().data(),
                                                  e.description().length()))
                           .arg(begin.line)
                           .arg(begin.column);

        // Show the offending line so the problem can be spotted at a glance.
        QFile file(m_config_file_path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            const QList<QByteArray> lines = file.readAll().split('\n');
            if (begin.line >= 1 && begin.line <= lines.size())
            {
                const QString src
                    = QString::fromUtf8(lines[begin.line - 1]).trimmed();
                text += QString(":\n\n    %1").arg(src);
            }
        }

        QMessageBox::critical(this, tr("Error in configuration file"),
                              tr("%1\n\n%2\n\nLoading default config.")
                                  .arg(m_config_file_path, text));
        initDefaultKeybinds();
        return;
    }
    catch (std::exception &e)
    {
        QMessageBox::critical(
            this, tr("Error in configuration file"),
            tr("There are one or more error(s) in your config "
               "file:\n%1\n\nLoading default config.")
                .arg(e.what()));
        // Fall back to defaults on parse error, same as the no-config path.
        initDefaultKeybinds();
        return;
    }

    // Misc
    if (auto misc = toml["misc"])
    {
        if (auto color_dialog_colors = misc["color_dialog_colors"].as_array())
        {
            m_config.misc.color_dialog_colors.clear();
            for (const auto &color_node : *color_dialog_colors)
            {
                if (auto color_str = color_node.value<std::string>())
                {
                    uint32_t color;
                    if (parseHexColor(*color_str, color))
                        m_config.misc.color_dialog_colors.push_back(
                            rgbaToQColor(color));
                }
            }
        }
    }

    applyViewToml(toml, m_config);

    // Per-file-type overrides: [filetype.pdf.behavior], [filetype.epub.layout]
    // ... Each is applied on top of a view's options when a document of that
    // type is opened in it.
    if (auto *filetypes = toml["filetype"].as_table())
    {
        auto overrides = std::make_shared<
            std::map<std::string, std::function<void(Config &)>>>();
        for (auto &[key, node] : *filetypes)
        {
            auto *section = node.as_table();
            if (!section)
                continue;
            const std::string name = QString::fromUtf8(key.data(), key.length())
                                         .toLower()
                                         .toStdString();
            static const QSet<QString> kSections
                = {"page",       "layout",   "zoom",        "selection",
                   "scrollbars", "search",   "jump_marker", "annotations",
                   "links",      "behavior", "reflow"};
            for (const auto &[sec, value] : *section)
            {
                const QString secName
                    = QString::fromUtf8(sec.data(), sec.length());
                if (!kSections.contains(secName))
                    configIssue(
                        &value,
                        QString("[filetype.%1]: unknown section '%2'")
                            .arg(QString::fromStdString(name), secName));
                else if (!value.is_table())
                    configIssue(
                        &value,
                        QString("[filetype.%1]: '%2' is a section; set "
                                "an option inside it, e.g. "
                                "%2.<option> = ...")
                            .arg(QString::fromStdString(name), secName));
            }
            auto table = std::make_shared<toml::table>(*section);
            // Read it once now so wrong values are reported at startup, not
            // silently the first time a document of this type is opened.
            {
                Config scratch = m_config;
                applyViewToml(*section, scratch); // the copy has no line info
            }
            (*overrides)[name] = [table](Config &cfg)
            {
                applyViewToml(*table, cfg);
                g_config_issues.clear(); // already reported at startup
            };
        }
        m_config.filetype_overrides = std::move(overrides);
    }

    // Portals
    if (auto portal = toml["portal"])
    {
        set_color(portal["border_color"], m_config.portal.border_color);
        set(portal["enabled"], m_config.portal.enabled);
        set(portal["border_width"], m_config.portal.border_width);
        set(portal["respect_parent"], m_config.portal.respect_parent);
        set(portal["dim_inactive"], m_config.portal.dim_inactive);
        set(portal["split"], m_config.portal.split);
    }

    // Preview
    if (auto preview = toml["preview"])
    {
        set(preview["border_radius"], m_config.preview.border_radius);
        set(preview["close_on_click_outside"],
            m_config.preview.close_on_click_outside);
        if (preview["size_ratio"].is_table())
        {
            float width{0.6}, height{0.7};

            const auto &size_table = *preview["size_ratio"].as_table();

            if (auto toml_width = size_table["width"].value<int>())
                width = *toml_width;
            if (auto toml_height = size_table["height"].value<int>())
                height = *toml_height;

            if (width > 0 && height > 0)
            {
                m_config.preview.size_ratio = {width, height};
            }
        }
        set(preview["opacity"], m_config.preview.opacity);
    }

    if (auto thumbnail_panel = toml["thumbnail_panel"])
    {
        set(thumbnail_panel["show_page_numbers"],
            m_config.thumbnail.show_page_numbers);
        set(thumbnail_panel["panel_width"], m_config.thumbnail.panel_width);
        set(thumbnail_panel["font_size"], m_config.thumbnail.font_size);
        set(thumbnail_panel["highlight_current_page"],
            m_config.thumbnail.highlight_current_page);
        set(thumbnail_panel["sync_scroll"], m_config.thumbnail.sync_scroll);
    }

    // Tabs
    if (auto tabs = toml["tabs"])
    {
        set(tabs["visible"], m_config.tabs.visible);
        set(tabs["auto_hide"], m_config.tabs.auto_hide);
        set(tabs["movable"], m_config.tabs.movable);

        if (auto str = tabs["close_button_mode"])
        {
            using CBM = Config::Tabs::CloseButtonMode;
            if (str == "current")
                m_config.tabs.close_button_mode = CBM::Current;
            else if (str == "hidden")
                m_config.tabs.close_button_mode = CBM::Hidden;
            else
                m_config.tabs.close_button_mode = CBM::All;
        }
        if (auto str = tabs["elide_mode"])
        {
            Qt::TextElideMode mode;
            if (str == "left")
                mode = Qt::ElideLeft;
            else if (str == "right")
                mode = Qt::ElideRight;
            else if (str == "middle")
                mode = Qt::ElideMiddle;
            else
                mode = Qt::ElideNone;
            m_config.tabs.elide_mode = mode;
        }

        if (auto str = tabs["location"])
        {
            QTabWidget::TabPosition location;

            if (str == "left")
                location = QTabWidget::West;
            else if (str == "right")
                location = QTabWidget::East;
            else if (str == "bottom")
                location = QTabWidget::South;
            else
                location = QTabWidget::North;

            m_config.tabs.location = location;
        }
        set(tabs["full_path"], m_config.tabs.full_path);
        set(tabs["lazy_load"], m_config.tabs.lazy_load);

        if (auto str = tabs["open_position"])
        {
            using OP = Config::Tabs::OpenPosition;
            if (str == "start")
                m_config.tabs.open_position = OP::Start;
            else if (str == "after_current")
                m_config.tabs.open_position = OP::AfterCurrent;
            else
                m_config.tabs.open_position = OP::End;
        }
    }

    // Window
    if (auto window = toml["window"])
    {
        set(window["startup_tab"], m_config.window.startup_tab);
        set(window["menubar"], m_config.window.menubar);
        set(window["show_menu_icons"], m_config.window.show_menu_icons);
        set(window["fullscreen"], m_config.window.fullscreen);
        set_color(window["accent"], m_config.window.accent);
        set_color(window["bg"], m_config.window.bg);

        if (window["initial_size"].is_table())
        {
            int width{600}, height{400};

            const auto &size_table = *window["initial_size"].as_table();

            if (auto toml_width = size_table["width"].value<int>())
                width = *toml_width;
            if (auto toml_height = size_table["height"].value<int>())
                height = *toml_height;

            if (width > 0 && height > 0)
                m_config.window.initial_size = {width, height};
        }

        if (m_config.window.fullscreen)
            this->showFullScreen();

        set_title_format_if_present(window["title_format"],
                                    m_config.window.title_format);
    }

    // Annotations

    // Statusbar
    if (auto statusbar = toml["statusbar"])
    {
        set(statusbar["visible"], m_config.statusbar.visible);

        // A list of four numbers (left, top, right, bottom), or one number
        // for all four sides.
        if (auto padding_array = statusbar["padding"].as_array();
            padding_array && padding_array->size() >= 4)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (auto v
                    = padding_array->get(static_cast<size_t>(i))->value<int>())
                    m_config.statusbar.padding[i] = *v;
            }
        }
        else if (auto all = statusbar["padding"].value<int>())
            m_config.statusbar.padding.fill(*all);
        else if (statusbar["padding"])
            configIssue(statusbar["padding"].node(),
                        QObject::tr("expected a number, or a list of four "
                                    "numbers (left, top, right, bottom)"));

        if (auto *order = statusbar["layout"].as_array())
        {
            m_config.statusbar.layout = tomlToVariant(*order).toList();
            QStringList problems;
            statusbar_layout::parse(m_config.statusbar.layout, &problems);
            for (const QString &problem : std::as_const(problems))
                configIssue(order, QObject::tr("layout: %1").arg(problem));
        }
        else if (statusbar["layout"])
            configIssue(statusbar["layout"].node(),
                        QObject::tr("expected a list for the layout"));

        if (auto components = statusbar["components"])
        {
            if (auto mode = components["mode"])
            {
                set(mode["show"], m_config.statusbar.component.mode.show);
                set(mode["text"], m_config.statusbar.component.mode.text);
                set(mode["icon"], m_config.statusbar.component.mode.icon);
            }

            if (auto pagenumber = components["pagenumber"])
            {
                set(pagenumber["show"],
                    m_config.statusbar.component.pagenumber.show);
            }

            if (auto session = components["session"])
            {
                set(session["show"], m_config.statusbar.component.session.show);
            }

            if (auto zoom = components["zoom"])
            {
                set(zoom["show"], m_config.statusbar.component.zoom.show);
            }

            if (auto filename = components["filename"])
            {
                set(filename["show"],
                    m_config.statusbar.component.filename.show);
                set(filename["full_path"],
                    m_config.statusbar.component.filename.full_path);
            }

            if (auto progress = components["progress"])
            {
                set(progress["show"],
                    m_config.statusbar.component.progress.show);
            }
        }
    }

    // Layout

    // Zoom

    // Selection

    /* scrollbars */

    // Picker
    if (auto picker = toml["picker"])
    {
        set_picker_shared(picker, m_config.picker);

        // Picker.Keys
        if (auto picker_keys = picker["keys"])
        {
            if (picker_keys.is_table())
            {
                const auto &keys = *picker_keys.as_table();
                const Picker::Keybindings defaults{};

                const auto parse
                    = [](const std::string &s) -> std::optional<QKeyCombination>
                {
                    const auto seq = QKeySequence::fromString(
                        QString::fromStdString(s), QKeySequence::PortableText);
                    if (seq.isEmpty())
                        return std::nullopt;
                    return seq[0];
                };

                const auto get = [&](std::string_view field,
                                     const QList<QKeyCombination> &fallback)
                    -> QList<QKeyCombination>
                {
                    const auto *node = keys.get(field);
                    if (!node)
                        return fallback;

                    if (node->is_string())
                    {
                        if (auto kc
                            = parse(std::string(node->as_string()->get())))
                            return {*kc};
                        return fallback;
                    }
                    else if (node->is_array())
                    {
                        QList<QKeyCombination> result;
                        for (const auto &elem : *node->as_array())
                            if (auto s = elem.value<std::string>())
                                if (auto kc = parse(*s))
                                    result << *kc;
                        return result.isEmpty() ? fallback : result;
                    }

                    return fallback;
                };

                m_picker_keybinds = Picker::Keybindings{
                    .moveUp      = get("up", defaults.moveUp),
                    .moveDown    = get("down", defaults.moveDown),
                    .pageUp      = get("page_up", defaults.pageUp),
                    .pageDown    = get("page_down", defaults.pageDown),
                    .sectionPrev = get("section_prev", defaults.sectionPrev),
                    .sectionNext = get("section_next", defaults.sectionNext),
                    .accept      = get("accept", defaults.accept),
                    .expand      = get("expand", defaults.expand),
                    .collapse    = get("collapse", defaults.collapse),
                    .dismiss     = get("dismiss", defaults.dismiss),
                    .toggleStructureMode = get("toggle_structure_mode",
                                               defaults.toggleStructureMode),
                    .historyPrev = get("history_prev", defaults.historyPrev),
                    .historyNext = get("history_next", defaults.historyNext),
                };
            }
        }
    }

    // Apply picker defaults to all picker-like sections.
    // Individual sections parsed below can still override these values.
    inherit_picker_defaults(m_config.picker, m_config.outline);
    inherit_picker_defaults(m_config.picker, m_config.highlight_search);
    inherit_picker_defaults(m_config.picker, m_config.command_palette);

    // Command Palette
    if (auto command_palette = toml["command_palette"])
    {
        set_picker_shared(command_palette, static_cast<Config::Picker &>(
                                               m_config.command_palette));

        set(command_palette["description"],
            m_config.command_palette.description);
        set(command_palette["vscrollbar"], m_config.command_palette.vscrollbar);
        // set(command_palette["show_grid"], m_config.command_palette.grid);
        // TODO: Implement grid in command palette
        set(command_palette["show_shortcuts"],
            m_config.command_palette.show_shortcuts);
        set(command_palette["prompt"], m_config.command_palette.prompt);
        set(command_palette["sort_by_frequency"],
            m_config.command_palette.sort_by_frequency);
        set(command_palette["persist_frequency"],
            m_config.command_palette.persist_frequency);
    }

    // Markers

    // Links

    // Link Hints
    if (auto link_hints = toml["link_hints"])
    {
        set(link_hints["size"], m_config.link_hints.size);
        set_color(link_hints["bg"], m_config.link_hints.bg);
        set_color(link_hints["fg"], m_config.link_hints.fg);
    }

    // Outline
    if (auto outline = toml["outline"])
    {
        set_picker_shared(outline,
                          static_cast<Config::Picker &>(m_config.outline));

        set(outline["indent_width"], m_config.outline.indent_width);
        set(outline["show_page_numbers"], m_config.outline.show_page_number);
        set(outline["flat_menu"], m_config.outline.flat_menu);
    }

    // Highlight Search
    if (auto highlight_search = toml["highlight_search"])
    {
        set_picker_shared(highlight_search, static_cast<Config::Picker &>(
                                                m_config.highlight_search));
        set(highlight_search["flat_menu"], m_config.highlight_search.flat_menu);
    }

    // Search

#ifdef WITH_SYNCTEX
    if (auto synctex = toml["synctex"])
    {
        set(synctex["enabled"], m_config.synctex.enabled);
        set(synctex["editor_command"], m_config.synctex.editor_command);
    }
#endif

    if (auto updates = toml["updates"])
    {
        set(updates["check"], m_config.updates.check);
        set(updates["whats_new"], m_config.updates.whats_new);
    }

    if (auto donate = toml["donate"])
        set(donate["reminders"], m_config.donate.reminders);

    if (auto llm_view = toml["llm_view"])
    {
        set(llm_view["show_at_startup"], m_config.llm_view.show_at_startup);
        set(llm_view["auto_run"], m_config.llm_view.auto_run);
        set(llm_view["tools"], m_config.llm_view.tools);
        set(llm_view["save_history"], m_config.llm_view.save_history);
        set(llm_view["font_size"], m_config.llm_view.font_size);
        set(llm_view["separate_window"], m_config.llm_view.separate_window);
        set(llm_view["dock_area"], m_config.llm_view.dock_area);
        set(llm_view["model"], m_config.llm_view.model);
        set(llm_view["api_key"], m_config.llm_view.api_key);
        set(llm_view["api_url"], m_config.llm_view.api_url);

        if (auto extra_body = llm_view["extra_body"])
        {
            m_config.llm_view.extra_body.clear();
            for (auto &[key, value] : *extra_body.as_table())
            {
                const QString qkey
                    = QString::fromStdString(std::string(key.str()));
                if (auto v = value.value<bool>())
                    m_config.llm_view.extra_body.insert(qkey, *v);
                else if (auto v = value.value<int64_t>())
                    m_config.llm_view.extra_body.insert(
                        qkey, static_cast<qlonglong>(*v));
                else if (auto v = value.value<double>())
                    m_config.llm_view.extra_body.insert(qkey, *v);
                else if (auto v = value.value<std::string>())
                    m_config.llm_view.extra_body.insert(
                        qkey, QString::fromStdString(*v));
            }
        }
    }

    // Rendering
    if (auto rendering = toml["rendering"])
    {
        set(rendering["antialiasing"], m_config.rendering.antialiasing);
        set(rendering["text_antialiasing"],
            m_config.rendering.text_antialiasing);
        set(rendering["smooth_pixmap_transform"],
            m_config.rendering.smooth_pixmap_transform);
        set(rendering["antialiasing_bits"],
            m_config.rendering.antialiasing_bits);

        if (auto v = rendering["backend"].value<std::string>())
        {
            if (*v == "opengl")
                m_config.rendering.backend = Config::Rendering::Backend::OpenGL;
            else if (*v == "raster")
                m_config.rendering.backend = Config::Rendering::Backend::Raster;
            else
                m_config.rendering.backend = Config::Rendering::Backend::Auto;
        }

        // If DPR is specified in config, use that (can be scalar or map)
        if (rendering["dpr"])
        {
            if (rendering["dpr"].is_value())
            {
                if (auto v = rendering["dpr"].value<float>())
                {
                    m_config.rendering.dpr = *v;
                    m_screen_dpr_map[QGuiApplication::primaryScreen()->name()]
                        = *v;
                }
            }
            else if (rendering["dpr"].is_table())
            {
                // Only build a map if table exists; else leave default
                auto dpr_table = rendering["dpr"];
                if (auto t = dpr_table.as_table())
                {
                    // Start from current map (if you want table to
                    // "add/override") or clear it (if you want table to
                    // "replace"). Here: replace, because that's what your
                    // old code effectively did.
                    m_screen_dpr_map.clear();
                    for (auto &[screen_name, value] : *t)
                    {
                        if (auto v = value.value<float>())
                        {
                            const QString screen_str = QString::fromStdString(
                                std::string(screen_name.str()));

                            for (QScreen *screen : QApplication::screens())
                            {
                                if (screen->name() == screen_str)
                                {
                                    m_screen_dpr_map[screen->name()] = *v;
                                    break;
                                }
                            }
                        }
                    }

                    m_config.rendering.dpr = m_screen_dpr_map;
                }
            }
        }
        else
        {
            m_screen_dpr_map[QGuiApplication::primaryScreen()->name()] = 1.0f;
        }
    }

    // Split
    if (auto split = toml["split"])
    {
        set(split["mouse_follows_focus"], m_config.split.mouse_follows_focus);
        set(split["focus_follows_mouse"], m_config.split.focus_follows_mouse);
        set(split["dim_inactive"], m_config.split.dim_inactive);
        set(split["dim_inactive_opacity"], m_config.split.dim_inactive_opacity);
        set(split["focus_border"], m_config.split.focus_border);
        set_color(split["focus_border_color"],
                  m_config.split.focus_border_color);
        set(split["focus_border_width"], m_config.split.focus_border_width);
        set(split["maximize_indicator"], m_config.split.maximize_indicator);
        set_color(split["maximize_indicator_color"],
                  m_config.split.maximize_indicator_color);
        set(split["gap"], m_config.split.gap);
    }

    // Behavior

    // Defaults are loaded here (exactly once) rather than in construct(),
    // so `load_defaults = false` in the user's [keybindings] block can
    // actually skip them. If there is no [keybindings] block at all,
    // defaults still load — the block being absent is not the same as
    // opting out.
    auto keybindings           = toml["keybindings"];
    const bool has_keybindings = static_cast<bool>(keybindings);
    const bool want_defaults
        = !has_keybindings || keybindings["load_defaults"].value_or(true);
    if (want_defaults)
        initDefaultKeybinds();

    if (has_keybindings)
    {
        for (auto &[action, value] : *keybindings.as_table())
        {
            // `load_defaults` is a config knob, not a command name — the
            // loop was previously calling setupKeybinding("load_defaults",
            // {"true"}), which silently no-op'd but still churned Qt state.
            if (action == "load_defaults")
                continue;

            if (value.is_value())
                setupKeybinding(
                    QString::fromStdString(std::string(action.str())),
                    {QString::fromStdString(value.value_or<std::string>(""))});
            else if (value.is_array())
            {
                QStringList keys;
                for (const auto &elem : *value.as_array())
                {
                    if (auto s = elem.value<std::string>())
                        keys << QString::fromStdString(*s);
                }
                setupKeybinding(
                    QString::fromStdString(std::string(action.str())), keys);
            }
        }
    }

    if (auto mbs = toml["mousebindings"])
    {
        for (auto &[action, value] : *mbs.as_table())
        {
            if (value.is_value())
            {
                setupMousebinding(
                    QString::fromStdString(std::string(action.str())),
                    QString::fromStdString(value.value_or<std::string>("")));
            }
        }
    }

    if (!g_config_issues.isEmpty())
    {
        constexpr int kMaxShown = 15;
        QStringList shown       = g_config_issues.mid(0, kMaxShown);
        if (g_config_issues.size() > kMaxShown)
            shown << tr("... and %1 more")
                         .arg(g_config_issues.size() - kMaxShown);
        for (const QString &issue : std::as_const(g_config_issues))
            qWarning().noquote() << "config:" << issue;
        QMessageBox::warning(this, tr("Problems in configuration file"),
                             tr("%1\n\nThese settings were ignored:\n\n%2")
                                 .arg(m_config_file_path, shown.join('\n')));
        g_config_issues.clear();
    }

#ifndef NDEBUG
    qDebug() << "Finished reading config file:" << m_config_file_path;
#endif
}

void
Lektra::initDefaultMousebinds() noexcept
{
    setupMousebinding("pan", "Alt+LeftButton");
    setupMousebinding("preview", "Alt+Shift+LeftButton");
    setupMousebinding("portal", "Ctrl+LeftButton");
#ifdef WITH_SYNCTEX
    setupMousebinding("synctex_jump", "Shift+LeftButton");
#endif
}

// Initialize the keybindings related stuff
void
Lektra::initDefaultKeybinds() noexcept
{
    struct DefaultBinding
    {
        const char *action;
        const char *key;
    };

    constexpr DefaultBinding defaults[] = {
        {"scroll_left", "h"},
        {"scroll_down", "j"},
        {"scroll_up", "k"},
        {"scroll_right", "l"},
        {"scroll_down_half_page", "Ctrl+d"},
        {"scroll_up_half_page", "Ctrl+u"},
        {"page_next", "Shift+j"},
        {"page_prev", "Shift+k"},
        {"page_first", "g,g"},
        {"page_last", "Shift+g"},
        {"page_goto", "Ctrl+g"},
        {"search", "/"},
        {"search_next", "n"},
        {"search_prev", "Shift+n"},
        {"zoom_in", "="},
        {"zoom_out", "-"},
        {"zoom_reset", "0"},
        {"fit_width", "Ctrl+Shift+W"},
        {"fit_height", "Ctrl+Shift+H"},
        {"fit_page", "Ctrl+Shift+="},
        {"fit_auto", "Ctrl+Shift+R"},
        {"picker_outline", "t"},
        {"picker_highlight_search", "Alt+Shift+H"},
        {"location_prev", "Ctrl+o"},
        {"location_next", "Ctrl+i"},
        {"selection_mode_text", "1"},
        {"annot_highlight_mode", "2"},
        {"annot_rect_mode", "3"},
        {"selection_mode_region", "4"},
        {"annot_popup_mode", "5"},
        {"link_hint_visit", "f"},
        {"file_open_tab", "o"},
        {"file_picker", "Ctrl+Shift+o"},
        {"file_save", "Ctrl+s"},
        {"visual_line_mode", "v"},
        {"undo", "u"},
        {"redo", "Ctrl+r"},
        {"invert_color", "i"},
        {"menubar", "Ctrl+Shift+m"},
        {"command_palette", ":"},
        {"run_last_command", "."},
        {"rotate_clock", ">"},
        {"rotate_anticlock", "<"},
        {"flip_horizontal", "|"},
        {"flip_vertical", "_"},
        {"tab_1", "Alt+1"},
        {"tab_2", "Alt+2"},
        {"tab_3", "Alt+3"},
        {"tab_4", "Alt+4"},
        {"tab_5", "Alt+5"},
        {"tab_6", "Alt+6"},
        {"tab_7", "Alt+7"},
        {"tab_8", "Alt+8"},
        {"tab_9", "Alt+9"},
        {"split_horizontal", "Ctrl+W,s"},
        {"split_vertical", "Ctrl+W,v"},
        {"split_focus_left", "Ctrl+W,h"},
        {"split_focus_right", "Ctrl+W,l"},
        {"split_focus_up", "Ctrl+W,k"},
        {"split_focus_down", "Ctrl+W,j"},
        {"split_close", "Ctrl+W,c"},
        {"files_recent", "Alt+Shift+o"},
        {"caret_mode", "F7"},
        {"caret_left", "Left"},
        {"caret_right", "Right"},
        {"caret_up", "Up"},
        {"caret_down", "Down"},
        {"caret_line_start", "Home"},
        {"caret_line_end", "End"},
        {"caret_select_left", "Shift+Left"},
        {"caret_select_right", "Shift+Right"},
        {"caret_select_up", "Shift+Up"},
        {"caret_select_down", "Shift+Down"},
        {"selection_copy", "y"},
    };

    for (const auto &binding : defaults)
    {
        setupKeybinding(QString::fromLatin1(binding.action),
                        {QString::fromLatin1(binding.key)});
    }

    // Vim visual-mode-style selection in caret mode: Shift+H/L alongside
    // Shift+Left/Right (Shift+J/K are handled specially in
    // Lektra::NextPage()/PrevPage(), since those keys are already bound to
    // page_next/page_prev outside caret mode).
    setupKeybinding("caret_select_left", {"Shift+Left", "Shift+H"});
    setupKeybinding("caret_select_right", {"Shift+Right", "Shift+L"});
}

void
Lektra::warnShortcutConflicts() noexcept
{
    QHash<QString, QStringList> shortcutsByKey;
    for (auto it = m_config.keybinds.constBegin();
         it != m_config.keybinds.constEnd(); ++it)
    {
        const QStringList keys = it.value();
        for (const QString &key : keys)
        {
            const QString trimmed = key.trimmed();
            if (trimmed.isEmpty())
                continue;

            const QKeySequence seq(trimmed);
            if (seq.isEmpty())
                continue;

            const QString normalized = seq.toString(QKeySequence::PortableText);
            if (normalized.isEmpty())
                continue;

            shortcutsByKey[normalized].append(it.key());
        }
    }

    QStringList conflicts;
    for (auto it = shortcutsByKey.constBegin(); it != shortcutsByKey.constEnd();
         ++it)
    {
        if (it.value().size() < 2)
            continue;

        QString keyDisplay
            = QKeySequence(it.key()).toString(QKeySequence::NativeText);
        if (keyDisplay.isEmpty())
            keyDisplay = it.key();

        const QString actions = it.value().join(", ");
        conflicts.append(tr("%1 -> %2").arg(keyDisplay, actions));
    }

    if (conflicts.isEmpty())
        return;

    const int maxItems = 3;
    QString message;
    if (conflicts.size() <= maxItems)
    {
        message = tr("Shortcut conflict(s): %1").arg(conflicts.join("; "));
    }
    else
    {
        message = tr("Shortcut conflict(s): %1; and %2 more")
                      .arg(conflicts.mid(0, maxItems).join("; "))
                      .arg(conflicts.size() - maxItems);
    }

    qWarning() << message;
    m_message_bar->showMessage(message, 6.0f);
}

void
Lektra::unsetKeybinding(const QString &action) noexcept
{
    const auto existing = findChildren<QShortcut *>(action);
    for (QShortcut *s : existing)
        delete s;

    m_config.keybinds.remove(action);
}

void
Lektra::setupKeybinding(const QString &action, const QStringList &keys) noexcept
{
    Command command = m_command_manager->find(action);
    if (command.name.isEmpty())
        return;

    const auto existing = findChildren<QShortcut *>(action);
    for (QShortcut *s : existing)
        delete s;

    m_config.keybinds.remove(action);

    // Fetch all shortcuts once — re-fetching inside the keys loop would do
    // a full child-tree traversal per key. We remove deleted entries
    // in-place so future key iterations never touch a dangling pointer.
    auto allShortcuts = findChildren<QShortcut *>();
    for (const QString &key : keys)
    {
        if (key.isEmpty())
            continue;

        const QKeySequence newSeq(key);
        const QString newSeqNormalized
            = newSeq.toString(QKeySequence::PortableText);
        if (newSeqNormalized.isEmpty())
            continue;

        for (int i = allShortcuts.size() - 1; i >= 0; --i)
        {
            QShortcut *s = allShortcuts[i];
            if (s->objectName() == action)
                continue;

            const QString existingNormalized
                = s->key().toString(QKeySequence::PortableText);
            if (existingNormalized != newSeqNormalized)
                continue;

            const QString otherAction = s->objectName();
            delete s;
            allShortcuts.removeAt(i);
            auto &otherKeys = m_config.keybinds[otherAction];
            otherKeys.removeAll(key);
            if (otherKeys.isEmpty())
                m_config.keybinds.remove(otherAction);
        }

        QShortcut *shortcut = new QShortcut(newSeq, this);
        shortcut->setObjectName(action);
        connect(shortcut, &QShortcut::activated,
                [command]() { command.action({}); });
#ifndef NDEBUG
        qDebug() << "Keybinding set:" << action << "->" << key;
#endif
        m_config.keybinds[action].append(key);
    }
}

void
Lektra::unsetMousebinding(const QString &action) noexcept
{
    Config::MouseBinding m = get_mouse_bind_key(action);
    m_config.mousebinds.erase(std::remove_if(m_config.mousebinds.begin(),
                                             m_config.mousebinds.end(),
                                             [&](const Config::MouseBinding &b)
    { return b.action == m.action; }),
                              m_config.mousebinds.end());
}

void
Lektra::setupMousebinding(const QString &action_str,
                          const QString &trigger) noexcept
{
#ifndef NDEBUG
    qDebug() << "Mousebinding set:" << action_str << "->" << trigger;
#endif

    Config::MouseBinding binding = get_mouse_bind_key(trigger);
    if (!binding.isValid())
    {
        qWarning() << tr("Invalid mouse binding for action") << action_str
                   << ":" << trigger;
        return;
    }

    if (action_str.isEmpty())
    {
        qWarning() << tr("Empty action for mouse binding with trigger:")
                   << trigger;
        return;
    }

    // Resolve action string to actual command
    GraphicsView::MouseAction action;

    if (action_str == "portal")
        action = GraphicsView::MouseAction::Portal;

#ifdef WITH_SYNCTEX
    else if (action_str == "synctex_jump")
        action = GraphicsView::MouseAction::SynctexJump;
#endif

    else if (action_str == "preview")
        action = GraphicsView::MouseAction::Preview;

    else if (action_str == "pan")
        action = GraphicsView::MouseAction::Pan;

    else
    {
        qWarning() << tr("Unknown action for mouse binding:") << action_str;
        return;
    }

    binding.action = action;
    m_config.mousebinds.push_back(binding);
}

void
Lektra::OpenConfigFile() noexcept
{
    const bool hasToml = QFile::exists(m_config_file_path);

#ifdef WITH_LUA
    const QString init_file = m_init_file_path.isEmpty()
                                  ? m_config_dir.filePath("init.lua")
                                  : m_init_file_path;
    const bool hasLua       = QFile::exists(init_file);

    if (hasToml && hasLua)
    {
        QMessageBox dlg(this);
        dlg.setWindowTitle(tr("Open Config"));
        dlg.setText(tr("Which config file would you like to open?"));
        QPushButton *tomlBtn
            = dlg.addButton("config.toml", QMessageBox::AcceptRole);
        QPushButton *luaBtn
            = dlg.addButton("init.lua", QMessageBox::AcceptRole);
        dlg.addButton(QMessageBox::Cancel);
        dlg.exec();

        if (dlg.clickedButton() == tomlBtn)
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_config_file_path));
        else if (dlg.clickedButton() == luaBtn)
            QDesktopServices::openUrl(QUrl::fromLocalFile(init_file));
        return;
    }

    if (hasLua)
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(init_file));
        return;
    }
#endif

    if (hasToml)
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_config_file_path));
        return;
    }

    QMessageBox::critical(
        this, tr("Error"),
        tr("Config file not found at:\n%1").arg(m_config_file_path));
}

#include "AboutDialog.hpp"
#include "AppPaths.hpp"
#include "ConfigOptions.hpp"
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
#include <algorithm>
#include <initializer_list>
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

void
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
    // The type was checked by is_integer() / is_floating_point() just above.
    if (node.is_integer())
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        return static_cast<qlonglong>(*node.value<int64_t>());
    if (node.is_floating_point())
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
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

template <typename T>
static inline void
inherit_picker_defaults(const Config::Picker &base, T &target)
{
    static_assert(std::is_base_of_v<Config::Picker, T>,
                  "Target must derive from Config::Picker");
    static_cast<Config::Picker &>(target) = base;
}
} // namespace

namespace co = config_options;

// One option from config.toml into its Config field.
static void
readOption(const co::Option &o, toml::node_view<toml::node> n, Config &cfg)
{
    switch (o.kind)
    {
        case co::Kind::Bool:
            set(n, o.ref<bool>(cfg));
            break;
        case co::Kind::Int:
        {
            int &v = o.ref<int>(cfg);
            set(n, v);
            v = std::clamp(v, o.rangeMin, o.rangeMax);
            break;
        }
        case co::Kind::Float:
            set(n, o.ref<float>(cfg));
            break;
        case co::Kind::Text:
            set(n, o.ref<QString>(cfg));
            break;
        case co::Kind::Color:
            set_color(n, o.ref<uint32_t>(cfg));
            break;
        case co::Kind::Choice:
        {
            int value = 0;
            if (auto s = n.value<std::string>())
            {
                if (o.choiceValue(*s, value))
                    o.setChoice(cfg, value);
                else
                    configIssue(n.node(),
                                QObject::tr("'%1' is not one of %2")
                                    .arg(QString::fromStdString(*s),
                                         o.choiceList()));
            }
            else
                configIssue(n.node(), QObject::tr("expected one of %1")
                                          .arg(o.choiceList()));
            break;
        }
        case co::Kind::Custom:
            break; // read by hand in initConfig()
    }
}

// Reads every option in `toml` that `want` accepts. Custom options are read
// by hand in initConfig().
static void
applyOptions(toml::table &toml, Config &cfg,
             bool (*want)(const co::Option &))
{
    for (const co::Option &o : co::all())
    {
        if (o.kind == co::Kind::Custom || !want(o))
            continue;
        if (auto n = toml.at_path(o.path))
            readOption(o, n, cfg);
        else if (!o.alias.empty())
            if (auto a = toml.at_path(o.alias))
                readOption(o, a, cfg);
    }
}

// Reports the keys under `table` (at `prefix`) that are not options.
// `scope` limits the options that count, e.g. to the per file type ones.
static void
checkUnknownKeys(const toml::table &table, const std::string &prefix,
                 unsigned scope, const QString &where,
                 std::initializer_list<std::string_view> skip = {})
{
    for (auto &&[key, value] : table)
    {
        if (std::find(skip.begin(), skip.end(), key.str()) != skip.end())
            continue;
        const std::string path = prefix.empty()
                                     ? std::string(key.str())
                                     : prefix + "." + std::string(key.str());
        if (const co::Option *o = co::find(path);
            o && (o->scope & scope) == scope)
            continue;
        if (co::hasChildren(path, scope))
        {
            if (const toml::table *sub = value.as_table())
                checkUnknownKeys(*sub, path, scope, where);
            else
                configIssue(&value,
                            QObject::tr("%1'%2' is a section; set an option "
                                        "inside it, e.g. %2.<option> = ...")
                                .arg(where, QString::fromStdString(path)));
            continue;
        }
        configIssue(&value, QObject::tr("%1unknown option '%2'")
                                .arg(where, QString::fromStdString(path)));
    }
}

static bool
isFiletypeOption(const co::Option &o)
{
    return o.scope & co::PerFiletype;
}

// [outline], [highlight_search] and [command_palette] are read after the
// [picker] values have been copied into them.
static bool
isPickerVariant(const co::Option &o)
{
    return o.path.starts_with("outline.")
           || o.path.starts_with("highlight_search.")
           || o.path.starts_with("command_palette.");
}

// annotations.<type>.glow.*, as opposed to the shared annotations.glow.*
static bool
isAnnotationTypeGlow(const co::Option &o)
{
    return o.path.starts_with("annotations.")
           && o.path.find(".glow.") != std::string::npos
           && !o.path.starts_with("annotations.glow.");
}

// annotations.glow.* is the glow of every annotation type. Each type takes
// the values `toml` sets there, then its own annotations.<type>.glow.* on top.
static void
applyAnnotationGlow(toml::table &toml, Config &cfg)
{
    auto &a = cfg.annotations;
    Config::Annotations::Base::Glow *types[]
        = {&a.highlight.glow, &a.underline.glow, &a.rect.glow, &a.popup.glow};
    for (auto *t : types)
    {
        if (toml.at_path("annotations.glow.enabled"))
            t->enabled = a.glow.enabled;
        if (toml.at_path("annotations.glow.color"))
            t->color = a.glow.color;
        if (toml.at_path("annotations.glow.width"))
            t->width = a.glow.width;
    }
    applyOptions(toml, cfg, isAnnotationTypeGlow);
}

// The options a [filetype.<type>] table can set, applied to `cfg`.
static void
applyFiletypeToml(toml::table &toml, Config &cfg)
{
    applyOptions(toml, cfg,
                 [](const co::Option &o)
                 { return isFiletypeOption(o) && !isAnnotationTypeGlow(o); });
    applyAnnotationGlow(toml, cfg);
    if (!cfg.reflow.font_family.isEmpty())
        Model::prewarmFontIndex();
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

        logError(tr("config: %1: %2").arg(m_config_file_path,
                                          QString(text).replace('\n', ' ')));
        QMessageBox::critical(this, tr("Error in configuration file"),
                              tr("%1\n\n%2\n\nLoading default config.")
                                  .arg(m_config_file_path, text));
        initDefaultKeybinds();
        return;
    }
    catch (std::exception &e)
    {
        logError(tr("config: %1: %2").arg(m_config_file_path,
                                          QString::fromUtf8(e.what())));
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

    // Every plain option (see ConfigOptions.cpp). The picker variants come
    // after [picker] has been copied into them, further down.
    applyOptions(toml, m_config,
                 [](const co::Option &o)
                 { return !isPickerVariant(o) && !isAnnotationTypeGlow(o); });
    applyAnnotationGlow(toml, m_config);
    if (!m_config.reflow.font_family.isEmpty())
        Model::prewarmFontIndex();

    // Keys that are not options. [filetype] is checked below; key and mouse
    // bindings take any command name.
    checkUnknownKeys(toml, {}, co::Global, {},
                     {"filetype", "keybindings", "mousebindings",
#ifndef WITH_SYNCTEX
                      "synctex", // valid, but this build has no SyncTeX
#endif
                     });

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
            checkUnknownKeys(
                *section, {}, co::PerFiletype,
                QString("[filetype.%1]: ").arg(QString::fromStdString(name)));
            auto table = std::make_shared<toml::table>(*section);
            // Read it once now so wrong values are reported at startup, not
            // silently the first time a document of this type is opened.
            {
                Config scratch = m_config;
                applyFiletypeToml(*section, scratch); // the copy has no line info
            }
            (*overrides)[name] = [table](Config &cfg)
            {
                applyFiletypeToml(*table, cfg);
                g_config_issues.clear(); // already reported at startup
            };
        }
        m_config.filetype_overrides = std::move(overrides);
    }

    // Options with a shape of their own (Kind::Custom in ConfigOptions.cpp)

    if (auto preview = toml["preview"]; preview["size_ratio"].is_table())
    {
        float width{0.6f}, height{0.7f};

        const auto &size_table = *preview["size_ratio"].as_table();

        if (auto toml_width = size_table["width"].value<float>())
            width = *toml_width;
        if (auto toml_height = size_table["height"].value<float>())
            height = *toml_height;

        if (width > 0 && height > 0)
            m_config.preview.size_ratio = {width, height};
    }

    if (auto window = toml["window"])
    {
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

    if (auto statusbar = toml["statusbar"])
    {
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

        if (const toml::node *order = statusbar["layout"].node();
            order && (order->is_array() || order->is_table()))
        {
            m_config.statusbar.layout
                = statusbar_layout::fromVariant(tomlToVariant(*order));
            QStringList problems;
            statusbar_layout::parse(m_config.statusbar.layout, &problems);
            for (const QString &problem : std::as_const(problems))
                configIssue(order, QObject::tr("layout: %1").arg(problem));
        }
        else if (statusbar["layout"])
            configIssue(statusbar["layout"].node(),
                        QObject::tr("expected a list or a table of sections for the layout"));
    }

    // [picker.keys]
    if (auto picker_keys = toml["picker"]["keys"]; picker_keys.is_table())
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
                if (auto kc = parse(std::string(node->as_string()->get())))
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
            .toggleStructureMode
            = get("toggle_structure_mode", defaults.toggleStructureMode),
            .historyPrev = get("history_prev", defaults.historyPrev),
            .historyNext = get("history_next", defaults.historyNext),
        };
    }

    // Apply picker defaults to all picker-like sections, then their own
    // values on top.
    inherit_picker_defaults(m_config.picker, m_config.outline);
    inherit_picker_defaults(m_config.picker, m_config.highlight_search);
    inherit_picker_defaults(m_config.picker, m_config.command_palette);
    applyOptions(toml, m_config, isPickerVariant);

#ifdef WITH_LLM_SUPPORT
    if (auto *extra_body = toml["llm_view"]["extra_body"].as_table())
    {
        m_config.llm_view.extra_body.clear();
        for (auto &[key, value] : *extra_body)
        {
            const QString qkey = QString::fromStdString(std::string(key.str()));
            if (auto v = value.value<bool>())
                m_config.llm_view.extra_body.insert(qkey, *v);
            else if (auto v = value.value<int64_t>())
                m_config.llm_view.extra_body.insert(qkey,
                                                    static_cast<qlonglong>(*v));
            else if (auto v = value.value<double>())
                m_config.llm_view.extra_body.insert(qkey, *v);
            else if (auto v = value.value<std::string>())
                m_config.llm_view.extra_body.insert(qkey,
                                                    QString::fromStdString(*v));
        }
    }
#endif

    if (auto presentation = toml["presentation"])
    {
        if (auto v = presentation["layout"].value<std::string>())
        {
            auto &layout = m_config.presentation.layout;
            if (*v == "keep")
                layout = std::nullopt;
            else if (*v == "vertical")
                layout = DocumentView::LayoutMode::VERTICAL;
            else if (*v == "horizontal")
                layout = DocumentView::LayoutMode::HORIZONTAL;
            else if (*v == "book")
                layout = DocumentView::LayoutMode::BOOK;
            else if (*v == "grid")
                layout = DocumentView::LayoutMode::GRID;
            else
                layout = DocumentView::LayoutMode::SINGLE;
        }

        if (auto allowed_actions = presentation["allowed_actions"].as_array())
        {
            m_config.presentation.allowed_actions.clear();
            for (const auto &elem : *allowed_actions)
                if (auto s = elem.value<std::string>())
                    m_config.presentation.allowed_actions
                        << QString::fromStdString(*s);
        }
    }

    if (auto rendering = toml["rendering"])
    {
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
            else if (auto *t = rendering["dpr"].as_table())
            {
                // A table replaces the whole per-screen map.
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
        else
        {
            m_screen_dpr_map[QGuiApplication::primaryScreen()->name()] = 1.0f;
        }
    }

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

    applyMessageLogSize();

    if (!g_config_issues.isEmpty())
    {
        constexpr int kMaxShown = 15;
        QStringList shown       = g_config_issues.mid(0, kMaxShown);
        if (g_config_issues.size() > kMaxShown)
            shown << tr("... and %1 more")
                         .arg(g_config_issues.size() - kMaxShown);
        for (const QString &issue : std::as_const(g_config_issues))
        {
            qWarning().noquote() << "config:" << issue;
            logWarning(QStringLiteral("config: ") + issue);
        }
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
        {"annot_underline_mode", "6"},
        {"annot_edit_mode", "7"},
        {"link_hint_visit", "f"},
        {"file_open_tab", "o"},
        {"file_picker", "Ctrl+Shift+o"},
        {"file_save", "Ctrl+s"},
        {"print", "Ctrl+p"},
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
        {"presentation_mode", "F5"},
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
        {"caret_word_next", "Ctrl+Right"},
        {"caret_word_prev", "Ctrl+Left"},
        {"caret_doc_start", "Ctrl+Home"},
        {"caret_doc_end", "Ctrl+End"},
        {"caret_select_word_next", "Ctrl+Shift+Right"},
        {"caret_select_word_prev", "Ctrl+Shift+Left"},
        {"caret_select_doc_start", "Ctrl+Shift+Home"},
        {"caret_select_doc_end", "Ctrl+Shift+End"},
        {"caret_highlight", "Ctrl+H"},
        {"caret_word_end", "e"},
        {"caret_find_char_back", "Shift+F"},
        {"caret_find_repeat", ";"},
        {"caret_find_repeat_reverse", ","},
        {"caret_locate", "F8"},
        {"caret_visual_line", "Shift+V"},
        {"selection_copy", "y"},
    };

    for (const auto &binding : defaults)
    {
        setupKeybinding(QString::fromLatin1(binding.action),
                        {QString::fromLatin1(binding.key)});
    }

    // Extra page-turn keys (clickers send PgUp/PgDown). Left/Right/Up/Down
    // stay on caret_*, which turn pages when caret mode is off.
    setupKeybinding("page_next", {"Shift+j", "PgDown", "Space"});
    setupKeybinding("page_prev", {"Shift+k", "PgUp"});

    // Vim visual-mode-style selection in caret mode: Shift+H/L alongside
    // Shift+Left/Right (Shift+J/K are handled specially in
    // Lektra::NextPage()/PrevPage(), since those keys are already bound to
    // page_next/page_prev outside caret mode).
    setupKeybinding("caret_select_left", {"Shift+Left", "Shift+H"});
    setupKeybinding("caret_select_right", {"Shift+Right", "Shift+L"});
    // Vim word motions (they do nothing outside caret mode)
    setupKeybinding("caret_word_next", {"Ctrl+Right", "w"});
    setupKeybinding("caret_word_prev", {"Ctrl+Left", "b"});
}

void
Lektra::warnShortcutConflicts() noexcept
{
    QHash<QString, QStringList> shortcutsByKey;
    for (auto it = m_config.keybinds.constBegin();
         it != m_config.keybinds.constEnd(); ++it)
    {
        const QStringList &keys = it.value();
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
    m_message_bar->showMessage(message, 6.0f, MessageLog::Level::Warning);
}

void
Lektra::unsetKeybinding(const QString &action) noexcept
{
    const auto existing = findChildren<QShortcut *>(action);
    for (QShortcut *s : existing)
        delete s;

    m_config.keybinds.remove(action);
}

// Actions that stay usable while presentation mode is active.
static bool
isPresentationAction(const QString &action,
                     const QStringList &extra) noexcept
{
    static const QSet<QString> allowed
        = {"page_next",  "page_prev",  "caret_left",
           "caret_right", "caret_up",  "caret_down",
           "presentation_mode", "laser_pointer_cursor"};
    return allowed.contains(action) || extra.contains(action);
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
        connect(shortcut, &QShortcut::activated, [this, action, command]()
        {
            // Presentation mode only allows navigation and leaving it.
            if (m_presentation.active && !isPresentationAction(
                                          action,
                                          m_config.presentation.allowed_actions))
                return;
            command.action({});
        });
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

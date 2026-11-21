#include "ConfigOptions.hpp"

#include "Config.hpp"
#include "Lektra.hpp"

#include <QObject>
#include <QStringList>
#include <type_traits>
#include <unordered_map>

// Every config option, in one list. config.toml (Config.cpp), Lua
// (`lektra.opt`, opt.cpp) and the schema generator all read this list.
//
// An entry names the option's key and the Config field it sets:
//
//     r.section("search", View);
//     r.boolean("progressive", F(search.progressive));
//
// The method says what kind of value the option takes and must match the
// field's C++ type (a mismatch does not compile):
//
//     boolean  bool        integer  int        number  float
//     text     QString     color    uint32_t   choice  an enum
//     custom   read by hand in Config.cpp and opt.cpp
//
// After an entry, `.alias("other_key")` accepts a second name,
// `.range(lo, hi)` clamps an integer and `.onChange(fn)` runs fn after a Lua
// write. Keys may be dotted ("glow.enabled") for sub-tables.
//
// The section's scope says where else it can be set: Global (only the global
// config), PerView (Lua view:opt()), PerFiletype ([filetype.<type>] in
// config.toml), or View (both).
//
// Keep each entry on one line: scripts/gen_config_schema.py reads this file.

namespace config_options
{

namespace
{

// The Config member an option sets, e.g. F(search.progressive).
#define F(member) [](Config &c) -> auto & { return c.member; }

class Registry
{
public:
    Registry &section(std::string prefix, unsigned scope,
                      std::string changed = {})
    {
        m_prefix  = std::move(prefix);
        m_scope   = scope;
        m_changed = changed.empty() ? m_prefix.substr(0, m_prefix.find('.'))
                                    : std::move(changed);
        return *this;
    }

    template <typename Get>
    Registry &boolean(const char *key, Get)
    {
        return typed<bool, Get>(Kind::Bool, key);
    }

    template <typename Get>
    Registry &integer(const char *key, Get)
    {
        return typed<int, Get>(Kind::Int, key);
    }

    template <typename Get>
    Registry &number(const char *key, Get)
    {
        return typed<float, Get>(Kind::Float, key);
    }

    template <typename Get>
    Registry &text(const char *key, Get)
    {
        return typed<QString, Get>(Kind::Text, key);
    }

    template <typename Get>
    Registry &color(const char *key, Get)
    {
        return typed<uint32_t, Get>(Kind::Color, key);
    }

    // An enum option written as one of `choices`.
    template <typename Get, size_t N>
    Registry &choice(const char *key, Get, const Choice (&choices)[N])
    {
        using E = std::remove_reference_t<std::invoke_result_t<Get, Config &>>;
        static_assert(std::is_enum_v<E>, "choice() needs an enum field");
        Option &o   = push(Kind::Choice, key);
        o.choices   = std::span<const Choice>(choices, N);
        o.getChoice = [](const Config &c)
        { return static_cast<int>(Get{}(const_cast<Config &>(c))); };
        o.setChoice
            = [](Config &c, int value) { Get{}(c) = static_cast<E>(value); };
        return *this;
    }

    Registry &custom(const char *key)
    {
        push(Kind::Custom, key);
        return *this;
    }

    // Modifiers for the entry just added.
    Registry &alias(const char *key)
    {
        m_options.back().alias = m_prefix + "." + key;
        return *this;
    }

    Registry &range(int lo, int hi)
    {
        m_options.back().rangeMin = lo;
        m_options.back().rangeMax = hi;
        return *this;
    }

    Registry &onChange(void (*fn)(Lektra *))
    {
        m_options.back().onChange = fn;
        return *this;
    }

    // Lua reads and writes this choice as a lektra.<Enum> number.
    Registry &luaNumber()
    {
        m_options.back().luaNumber = true;
        return *this;
    }

    std::vector<Option> take()
    {
        return std::move(m_options);
    }

private:
    Option &push(Kind kind, const char *key)
    {
        Option o;
        o.path    = m_prefix + "." + key;
        o.section = m_changed;
        o.kind    = kind;
        o.scope   = m_scope;
        m_options.push_back(std::move(o));
        return m_options.back();
    }

    template <typename T, typename Get>
    Registry &typed(Kind kind, const char *key)
    {
        using Field
            = std::remove_reference_t<std::invoke_result_t<Get, Config &>>;
        static_assert(std::is_same_v<Field, T>,
                      "the option's kind does not match its field's type");
        push(kind, key).field
            = [](Config &c) -> void * { return &Get{}(c); };
        return *this;
    }

    std::string m_prefix, m_changed;
    unsigned m_scope = Global;
    std::vector<Option> m_options;
};

// ---------------------------------------------------------------------------
// Choices
// ---------------------------------------------------------------------------

using LM = DocumentView::LayoutMode;
using FM = DocumentView::FitMode;

constexpr Choice kUnderlineStyle[] = {
    {"solid", (int)Config::Annotations::Underline::Style::Solid},
    {"dashed", (int)Config::Annotations::Underline::Style::Dashed},
    {"dotted", (int)Config::Annotations::Underline::Style::Dotted},
};

constexpr Choice kHoverHighlight[] = {
    {"block", (int)Config::Links::HoverPreview::Highlight::Block},
    {"line", (int)Config::Links::HoverPreview::Highlight::Line},
    {"marker", (int)Config::Links::HoverPreview::Highlight::Marker},
    {"none", (int)Config::Links::HoverPreview::Highlight::None},
};

constexpr Choice kLayoutMode[] = {
    {"vertical", (int)LM::VERTICAL}, {"single", (int)LM::SINGLE},
    {"horizontal", (int)LM::HORIZONTAL}, {"book", (int)LM::BOOK},
    {"grid", (int)LM::GRID},
};

constexpr Choice kFitMode[] = {
    {"width", (int)FM::Width},
    {"height", (int)FM::Height},
    {"window", (int)FM::Window},
};

constexpr Choice kMinimapPosition[] = {
    {"top_left", (int)Config::Minimap::Position::TopLeft},
    {"top_right", (int)Config::Minimap::Position::TopRight},
    {"bottom_left", (int)Config::Minimap::Position::BottomLeft},
    {"bottom_right", (int)Config::Minimap::Position::BottomRight},
};

constexpr Choice kCloseButtonMode[] = {
    {"all", (int)Config::Tabs::CloseButtonMode::All},
    {"current", (int)Config::Tabs::CloseButtonMode::Current},
    {"hidden", (int)Config::Tabs::CloseButtonMode::Hidden},
};

constexpr Choice kElideMode[] = {
    {"none", (int)Qt::ElideNone},
    {"left", (int)Qt::ElideLeft},
    {"right", (int)Qt::ElideRight},
    {"middle", (int)Qt::ElideMiddle},
};

constexpr Choice kTabLocation[] = {
    {"top", (int)QTabWidget::North},
    {"bottom", (int)QTabWidget::South},
    {"left", (int)QTabWidget::West},
    {"right", (int)QTabWidget::East},
};

constexpr Choice kOpenPosition[] = {
    {"end", (int)Config::Tabs::OpenPosition::End},
    {"start", (int)Config::Tabs::OpenPosition::Start},
    {"after_current", (int)Config::Tabs::OpenPosition::AfterCurrent},
};

constexpr Choice kBackend[] = {
    {"auto", (int)Config::Rendering::Backend::Auto},
    {"raster", (int)Config::Rendering::Backend::Raster},
    {"opengl", (int)Config::Rendering::Backend::OpenGL},
};

constexpr Choice kSaveMode[] = {
    {"overwrite", (int)Config::Behavior::SaveMode::Overwrite},
    {"ask", (int)Config::Behavior::SaveMode::Ask},
    {"suffix", (int)Config::Behavior::SaveMode::Suffix},
};

// ---------------------------------------------------------------------------
// Groups of options several sections share. `Base` is an F(...) accessor
// for the shared struct; S(member) reaches a member below it.
// ---------------------------------------------------------------------------

#define S(member) [](Config &c) -> auto & { return Base{}(c).member; }

// Config::Picker: [picker], [outline], [highlight_search], [command_palette]
template <typename Base>
void
pickerOptions(Registry &r, Base)
{
    r.number("width", S(width));
    r.number("height", S(height));
    r.boolean("border", S(border));
    r.boolean("alternating_row_color", S(alternating_row_color));
    r.text("prompt", S(prompt));
    r.boolean("highlight_matches.enabled", S(highlight_matches.enabled));
    r.color("highlight_matches.color", S(highlight_matches.color));
    r.boolean("shadow.enabled", S(shadow.enabled));
    r.integer("shadow.blur_radius", S(shadow.blur_radius));
    r.integer("shadow.offset_x", S(shadow.offset_x));
    r.integer("shadow.offset_y", S(shadow.offset_y));
    r.integer("shadow.opacity", S(shadow.opacity));
}

// Config::Annotations::Base::Glow
template <typename Base>
void
annotationGlow(Registry &r, Base)
{
    r.boolean("glow.enabled", S(glow.enabled));
    r.color("glow.color", S(glow.color));
    r.integer("glow.width", S(glow.width));
}

// Config::Annotations::Base::Comment
template <typename Base>
void
annotationComment(Registry &r, Base)
{
    r.boolean("comment.tooltip", S(comment.tooltip));
    r.boolean("comment.marker", S(comment.marker));
    r.integer("comment.font_size", S(comment.font_size));
}

#undef S

void
applyStatusbarLayout(Lektra *lk)
{
    lk->applyStatusbarLayout();
}

// ---------------------------------------------------------------------------
// The options
// ---------------------------------------------------------------------------

std::vector<Option>
build()
{
    Registry r;

    r.section("page", View);
    r.color("bg", F(page.bg));
    r.color("fg", F(page.fg));

#ifdef WITH_SYNCTEX
    r.section("synctex", Global);
    r.boolean("enabled", F(synctex.enabled));
    r.text("editor_command", F(synctex.editor_command));
#endif

    r.section("search", View);
    r.color("match_color", F(search.match_color));
    r.color("index_color", F(search.index_color));
    r.boolean("progressive", F(search.progressive));
    r.boolean("highlight_matches", F(search.highlight_matches));
    r.boolean("absolute_jump", F(search.absolute_jump));

    r.section("annotations.highlight", View);
    r.color("color", F(annotations.highlight.color));
    annotationGlow(r, F(annotations.highlight));
    annotationComment(r, F(annotations.highlight));

    r.section("annotations.underline", View);
    r.color("color", F(annotations.underline.color));
    r.number("thickness", F(annotations.underline.thickness));
    r.number("offset", F(annotations.underline.offset));
    r.choice("style", F(annotations.underline.style), kUnderlineStyle);
    annotationGlow(r, F(annotations.underline));
    annotationComment(r, F(annotations.underline));

    r.section("annotations.rect", View);
    r.color("color", F(annotations.rect.color));
    annotationGlow(r, F(annotations.rect));
    annotationComment(r, F(annotations.rect));

    r.section("annotations.redact", View);
    r.boolean("save_to_new_file", F(annotations.redact.save_to_new_file));

    // Popups have no comment marker.
    r.section("annotations.popup", View);
    annotationGlow(r, F(annotations.popup));
    r.boolean("comment.tooltip", F(annotations.popup.comment.tooltip));
    r.integer("comment.font_size", F(annotations.popup.comment.font_size));

    r.section("thumbnail_panel", Global, "thumbnail");
    r.boolean("page_numbers", F(thumbnail.page_numbers));
    r.number("panel_width", F(thumbnail.panel_width));
    r.integer("font_size", F(thumbnail.font_size));
    r.boolean("highlight_current_page", F(thumbnail.highlight_current_page));
    r.boolean("sync_scroll", F(thumbnail.sync_scroll));

    r.section("portal", Global);
    r.boolean("enabled", F(portal.enabled));
    r.color("border_color", F(portal.border_color));
    r.integer("border_width", F(portal.border_width));
    r.boolean("respect_parent", F(portal.respect_parent));
    r.boolean("dim_inactive", F(portal.dim_inactive));
    r.text("split", F(portal.split));

    r.section("window", Global);
    r.boolean("startup_tab", F(window.startup_tab));
    r.boolean("fullscreen", F(window.fullscreen));
    r.color("accent", F(window.accent));
    r.color("bg", F(window.bg)).onChange([](Lektra *lk) { lk->applyWindowBackground(); });
    r.custom("initial_size");
    r.custom("title_format");

    r.section("layout", View);
    r.choice("mode", F(layout.mode), kLayoutMode).luaNumber();
    r.integer("grid_columns", F(layout.grid_columns)).range(1, 32);
    r.choice("initial_fit", F(layout.initial_fit), kFitMode).luaNumber();
    r.boolean("auto_resize", F(layout.auto_resize));
    r.integer("spacing", F(layout.spacing));

    r.section("statusbar", Global);
    r.boolean("show", F(statusbar.show));
    r.custom("padding").onChange(applyStatusbarLayout);
    r.custom("layout").onChange(applyStatusbarLayout);
    r.boolean("components.mode.show", F(statusbar.component.mode.show));
    r.boolean("components.mode.text", F(statusbar.component.mode.text));
    r.boolean("components.mode.icon", F(statusbar.component.mode.icon));
    r.boolean("components.pagenumber.show", F(statusbar.component.pagenumber.show));
    r.boolean("components.session.show", F(statusbar.component.session.show));
    r.boolean("components.zoom.show", F(statusbar.component.zoom.show));
    r.boolean("components.filename.show", F(statusbar.component.filename.show));
    r.boolean("components.filename.full_path", F(statusbar.component.filename.full_path));
    r.boolean("components.progress.show", F(statusbar.component.progress.show));

    r.section("zoom", View);
    r.number("level", F(zoom.level));
    r.number("factor", F(zoom.factor));
    r.boolean("anchor_to_mouse", F(zoom.anchor_to_mouse));

    r.section("reflow", View);
    r.text("font_family", F(reflow.font_family));
    r.number("font_size", F(reflow.font_size));
    r.number("line_spacing", F(reflow.line_spacing));

    r.section("selection", View);
    r.integer("drag_threshold", F(selection.drag_threshold));
    r.boolean("copy_on_select", F(selection.copy_on_select));
    r.color("color", F(selection.color));

    r.section("split", PerView);
    r.boolean("mouse_follows_focus", F(split.mouse_follows_focus));
    r.boolean("focus_follows_mouse", F(split.focus_follows_mouse));
    r.integer("gap", F(split.gap));
    r.boolean("dim_inactive.enabled", F(split.dim_inactive.enabled));
    r.number("dim_inactive.opacity", F(split.dim_inactive.opacity));
    r.boolean("focus_border.enabled", F(split.focus_border.enabled));
    r.color("focus_border.color", F(split.focus_border.color));
    r.integer("focus_border.width", F(split.focus_border.width));
    r.boolean("maximize_indicator.enabled", F(split.maximize_indicator.enabled));
    r.color("maximize_indicator.color", F(split.maximize_indicator.color));

    r.section("scrollbars", View);
    r.boolean("vertical", F(scrollbars.vertical));
    r.boolean("horizontal", F(scrollbars.horizontal));
    r.boolean("search_hits", F(scrollbars.search_hits));
    r.boolean("auto_hide", F(scrollbars.auto_hide));
    r.integer("size", F(scrollbars.size));
    r.number("hide_timeout", F(scrollbars.hide_timeout));

    r.section("minimap", View);
    r.boolean("enabled", F(minimap.enabled));
    r.boolean("only_focused", F(minimap.only_focused));
    r.integer("size", F(minimap.size));
    r.integer("margin", F(minimap.margin));
    r.number("opacity", F(minimap.opacity));
    r.boolean("animated", F(minimap.animated));
    r.boolean("render_page", F(minimap.render_page));
    r.choice("position", F(minimap.position), kMinimapPosition);
    r.integer("border_width", F(minimap.border_width));
    r.color("border_color", F(minimap.border_color));
    r.number("viewport.width", F(minimap.viewport.width));
    r.color("viewport.color", F(minimap.viewport.color));
    r.color("viewport.fill", F(minimap.viewport.fill));
    r.color("viewport.dim_color", F(minimap.viewport.dim_color));

    r.section("jump_marker", View);
    r.boolean("enabled", F(jump_marker.enabled));
    r.color("jump_marker", F(jump_marker.color)).alias("color");
    r.number("fade_duration", F(jump_marker.fade_duration));

    r.section("links", View);
    r.boolean("enabled", F(links.enabled));
    r.boolean("boundary", F(links.boundary));
    r.boolean("detect_urls", F(links.detect_urls));
    r.text("url_regex", F(links.url_regex));
    r.boolean("hover_preview.enabled", F(links.hover_preview.enabled));
    r.integer("hover_preview.delay", F(links.hover_preview.delay));
    r.integer("hover_preview.width", F(links.hover_preview.width));
    r.integer("hover_preview.height", F(links.hover_preview.height));
    r.choice("hover_preview.highlight", F(links.hover_preview.highlight), kHoverHighlight);

    r.section("link_hints", PerView);
    r.number("size", F(link_hints.size));
    r.color("bg", F(link_hints.bg));
    r.color("fg", F(link_hints.fg));
    r.text("charset", F(link_hints.charset));

    r.section("tabs", Global);
    r.boolean("show", F(tabs.show));
    r.boolean("auto_hide", F(tabs.auto_hide));
    r.boolean("movable", F(tabs.movable));
    r.choice("close_button_mode", F(tabs.close_button_mode), kCloseButtonMode);
    r.boolean("scroll_text_on_hover", F(tabs.scroll_text_on_hover));
    r.integer("min_width", F(tabs.min_width));
    r.integer("max_width", F(tabs.max_width));
    r.choice("elide_mode", F(tabs.elide_mode), kElideMode);
    r.choice("location", F(tabs.location), kTabLocation);
    r.boolean("full_path", F(tabs.full_path));
    r.boolean("lazy_load", F(tabs.lazy_load));
    r.choice("open_position", F(tabs.open_position), kOpenPosition);

    // [outline], [highlight_search] and [command_palette] start from the
    // [picker] values, then apply their own (see Config.cpp).
    r.section("picker", Global);
    pickerOptions(r, F(picker));
    r.custom("keys.up");
    r.custom("keys.down");
    r.custom("keys.page_up");
    r.custom("keys.page_down");
    r.custom("keys.section_prev");
    r.custom("keys.section_next");
    r.custom("keys.accept");
    r.custom("keys.expand");
    r.custom("keys.collapse");
    r.custom("keys.dismiss");
    r.custom("keys.toggle_structure_mode");
    r.custom("keys.history_prev");
    r.custom("keys.history_next");

    r.section("outline", Global);
    pickerOptions(r, F(outline));
    r.integer("indent_width", F(outline.indent_width));
    r.boolean("page_numbers", F(outline.page_numbers));
    r.boolean("flat_menu", F(outline.flat_menu));
    r.boolean("preload", F(outline.preload));
    r.number("generate_heading_ratio", F(outline.generate_heading_ratio));
    r.integer("generate_max_levels", F(outline.generate_max_levels));

    r.section("highlight_search", Global);
    pickerOptions(r, F(highlight_search));
    r.boolean("flat_menu", F(highlight_search.flat_menu));

    r.section("command_palette", Global);
    pickerOptions(r, F(command_palette));
    r.boolean("description", F(command_palette.description));
    r.boolean("vscrollbar", F(command_palette.vscrollbar));
    r.boolean("shortcuts", F(command_palette.shortcuts));
    r.boolean("sort_by_frequency", F(command_palette.sort_by_frequency));
    r.boolean("persist_frequency", F(command_palette.persist_frequency));

    r.section("rendering", PerView);
    r.boolean("text_antialiasing", F(rendering.text_antialiasing));
    r.boolean("smooth_pixmap_transform", F(rendering.smooth_pixmap_transform));
    r.boolean("antialiasing.enabled", F(rendering.antialiasing.enabled));
    r.integer("antialiasing.bits", F(rendering.antialiasing.bits));
    r.boolean("page_shadow.enabled", F(rendering.page_shadow.enabled));
    r.integer("page_shadow.radius", F(rendering.page_shadow.radius));
    r.integer("page_shadow.offset_y", F(rendering.page_shadow.offset_y));
    r.color("page_shadow.color", F(rendering.page_shadow.color));
    r.choice("backend", F(rendering.backend), kBackend).luaNumber();
    r.custom("dpr");

    r.section("behavior", View);
    r.integer("preload_pages", F(behavior.preload_pages));
    r.boolean("confirm_on_quit", F(behavior.confirm_on_quit));
    r.integer("undo_limit", F(behavior.undo_limit));
    r.boolean("remember_last_visited", F(behavior.remember_last_visited));
    r.boolean("open_last_visited", F(behavior.open_last_visited));
    r.boolean("single_instance", F(behavior.single_instance));
    r.integer("page_history", F(behavior.page_history_limit)).alias("page_history_limit");
    r.boolean("invert_mode", F(behavior.invert_mode));
    r.boolean("dont_invert_images", F(behavior.dont_invert_images));
    r.boolean("auto_reload", F(behavior.auto_reload));
    r.boolean("cache_password", F(behavior.cache_password));
    r.boolean("recent_files", F(behavior.recent_files));
    r.integer("num_recent_files", F(behavior.num_recent_files));
    r.integer("cache_pages", F(behavior.cache_pages));
    r.integer("text_cache_size", F(behavior.text_cache_size));
    r.integer("stext_page_cache_size", F(behavior.stext_page_cache_size));
    r.integer("message_log_size", F(behavior.message_log_size)).range(0, 1000000).onChange([](Lektra *lk) { lk->applyMessageLogSize(); });
    r.integer("mupdf_store_size", F(behavior.mupdf_store_size));
    r.boolean("auto_scroll", F(behavior.auto_scroll));
    r.boolean("keep_view", F(behavior.keep_view));
    r.boolean("drag_image_out", F(behavior.drag_image_out));
    r.choice("save_mode", F(behavior.save_mode), kSaveMode);
    r.boolean("close_on_last_tab", F(behavior.close_on_last_tab));
    r.boolean("high_contrast.enabled", F(behavior.high_contrast.enabled));
    r.integer("high_contrast.black_point", F(behavior.high_contrast.black_point));
    r.integer("high_contrast.white_point", F(behavior.high_contrast.white_point));

    r.section("preview", Global);
    r.integer("border_radius", F(preview.border_radius));
    r.boolean("close_on_click_outside", F(preview.close_on_click_outside));
    r.number("opacity", F(preview.opacity));
    r.custom("size_ratio");

    r.section("updates", Global);
    r.boolean("check", F(updates.check));
    r.boolean("whats_new", F(updates.whats_new));

    r.section("donate", Global);
    r.boolean("reminders", F(donate.reminders));

    r.section("llm_view", Global);
    r.boolean("at_startup", F(llm_view.at_startup));
    r.boolean("auto_run", F(llm_view.auto_run));
    r.boolean("tools", F(llm_view.tools));
    r.boolean("save_history", F(llm_view.save_history));
    r.number("font_size", F(llm_view.font_size));
    r.boolean("separate_window", F(llm_view.separate_window));
    r.text("dock_area", F(llm_view.dock_area));
    r.text("model", F(llm_view.model));
    r.text("api_key", F(llm_view.api_key));
    r.text("api_url", F(llm_view.api_url));
    r.custom("extra_body");

    r.section("menubar", Global);
    r.boolean("show", F(menubar.show));
    r.boolean("icons", F(menubar.icons));

    r.section("presentation", Global);
    r.integer("hide_cursor_ms", F(presentation.hide_cursor_ms));
    r.boolean("fullscreen", F(presentation.fullscreen));
    r.boolean("show_statusbar", F(presentation.show_statusbar));
    r.boolean("laser_on_start", F(presentation.laser_on_start));
    r.choice("fit_mode", F(presentation.fit_mode), kFitMode).luaNumber();
    r.custom("layout");
    r.custom("allowed_actions");

    r.section("laser_pointer", Global);
    r.color("color", F(laser_pointer.color));
    r.integer("size", F(laser_pointer.size));
    r.boolean("trail.enabled", F(laser_pointer.trail.enabled));
    r.integer("trail.fade_ms", F(laser_pointer.trail.fade_ms));

    r.section("misc", Global);
    r.custom("color_dialog_colors");

    return r.take();
}

#undef F

struct Index
{
    std::unordered_map<std::string_view, const Option *> byPath;
    // prefix ("annotations", "annotations.rect", ...) -> the scopes of the
    // options below it, OR-ed together. A section's options share one scope.
    std::unordered_map<std::string, unsigned> prefixes;
};

const Index &
index()
{
    static const Index idx = []
    {
        Index i;
        for (const Option &o : all())
        {
            i.byPath.emplace(o.path, &o);
            if (!o.alias.empty())
                i.byPath.emplace(o.alias, &o);
            for (size_t dot = o.path.find('.'); dot != std::string::npos;
                 dot        = o.path.find('.', dot + 1))
                i.prefixes[o.path.substr(0, dot)] |= o.scope;
        }
        return i;
    }();
    return idx;
}

} // namespace

const std::vector<Option> &
all()
{
    static const std::vector<Option> options = build();
    return options;
}

const Option *
find(std::string_view path)
{
    const auto &byPath = index().byPath;
    const auto it      = byPath.find(path);
    return it == byPath.end() ? nullptr : it->second;
}

bool
hasChildren(std::string_view prefix, unsigned scope)
{
    const auto &prefixes = index().prefixes;
    const auto it        = prefixes.find(std::string(prefix));
    return it != prefixes.end() && (it->second & scope) == scope;
}

const char *
Option::choiceName(int value) const noexcept
{
    for (const Choice &c : choices)
        if (c.value == value)
            return c.name;
    return nullptr;
}

bool
Option::choiceValue(std::string_view name, int &value) const noexcept
{
    for (const Choice &c : choices)
    {
        if (name == c.name)
        {
            value = c.value;
            return true;
        }
    }
    return false;
}

QString
Option::choiceList() const
{
    QStringList names;
    for (const Choice &c : choices)
        names << QStringLiteral("\"%1\"").arg(QLatin1String(c.name));
    if (names.size() < 2)
        return names.join(QString());
    const QString last = names.takeLast();
    return QObject::tr("%1 or %2").arg(names.join(QStringLiteral(", ")), last);
}

} // namespace config_options

#pragma once

// The list of config options, shared by the config.toml loader
// (src/lektra/Config.cpp), the Lua `lektra.opt` tables (src/lua/opt.cpp) and
// the schema generator (scripts/gen_config_schema.py).
//
// To add an option, add its field to Config.hpp and one line to the list in
// src/lektra/ConfigOptions.cpp. Both config.toml and Lua then accept it.

#include <QString>
#include <climits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct Config;
class Lektra;

namespace config_options
{

enum class Kind
{
    Bool,   // bool
    Int,    // int
    Float,  // float
    Text,   // QString
    Color,  // uint32_t, written "#RRGGBB" or "#RRGGBBAA"
    Choice, // an enum, written as one of a few names
    Custom, // read by hand in Config.cpp and opt.cpp
};

// Where an option can be set besides the global config.
enum Scope : unsigned
{
    Global      = 0,
    PerView     = 1u << 0, // each view has its own copy (Lua view:opt())
    PerFiletype = 1u << 1, // can be set under [filetype.<type>] in config.toml
    View        = PerView | PerFiletype,
};

struct Choice
{
    const char *name;
    int value;
};

struct Option
{
    std::string path;    // "search.absolute_jump", the same in TOML and Lua
    std::string alias;   // another accepted path ("" if none)
    std::string section; // what DocumentView::localConfigChanged() gets
    Kind kind;
    unsigned scope;

    // The field. Its C++ type follows from `kind` (see Kind); Choice and
    // Custom options have none.
    void *(*field)(Config &) = nullptr;

    // Kind::Choice
    std::span<const Choice> choices;
    int (*getChoice)(const Config &)  = nullptr;
    void (*setChoice)(Config &, int)  = nullptr;
    bool luaNumber                    = false; // Lua uses lektra.<Enum> numbers

    // Kind::Int: values are clamped to this range
    int rangeMin = INT_MIN;
    int rangeMax = INT_MAX;

    void (*onChange)(Lektra *) = nullptr; // run after a Lua write

    template <typename T>
    T &ref(Config &config) const
    {
        return *static_cast<T *>(field(config));
    }

    const char *choiceName(int value) const noexcept;
    bool choiceValue(std::string_view name, int &value) const noexcept;
    QString choiceList() const; // "a", "b" or "c"
};

const std::vector<Option> &all();

// The option at `path` (or its alias), or nullptr.
const Option *find(std::string_view path);

// True if some option lives below `prefix`, e.g. "annotations.rect".
// With `scope` set, only options having all those scope bits count.
bool hasChildren(std::string_view prefix, unsigned scope = Global);

} // namespace config_options

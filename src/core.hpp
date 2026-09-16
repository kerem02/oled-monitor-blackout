#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oled {
struct Point { int x{}, y{}; };
struct Rect {
    int left{}, top{}, right{}, bottom{};
    int width() const noexcept {
        const auto value = static_cast<std::int64_t>(right) - left;
        return value > 0 && value <= std::numeric_limits<int>::max() ? static_cast<int>(value) : 0;
    }
    int height() const noexcept {
        const auto value = static_cast<std::int64_t>(bottom) - top;
        return value > 0 && value <= std::numeric_limits<int>::max() ? static_cast<int>(value) : 0;
    }
    bool valid() const noexcept { return width() > 0 && height() > 0; }
    bool contains(Point p) const noexcept {
        return valid() && p.x >= left && p.x < right && p.y >= top && p.y < bottom;
    }
    bool operator==(const Rect& r) const noexcept {
        return left == r.left && top == r.top && right == r.right && bottom == r.bottom;
    }
};
enum class State { Disabled, Unavailable, Paused, OnDisplay, Waiting, BlackedOut };
const char* stateName(State s) noexcept;
enum class DisplayPowerTransition { None, BecameOff, BecameOn };
DisplayPowerTransition displayPowerTransition(bool wasOff, unsigned reportedState) noexcept;
struct Input {
    bool enabled{true}, available{}, paused{}, cursorValid{};
    Rect bounds{};
    Point cursor{};
    std::uint64_t nowMs{};
    unsigned delaySeconds{30};
};
class StateMachine {
public:
    State step(const Input& input) noexcept;
    void reset() noexcept { awaySince_.reset(); state_ = State::Paused; }
    State state() const noexcept { return state_; }
private:
    State state_{State::Paused};
    std::optional<std::uint64_t> awaySince_;
};
struct Settings {
    bool enabled{true};
    unsigned delaySeconds{30};
    bool hotkeyEnabled{true}; // Ctrl+Alt+B; optional, conflict is visible in UI.
    std::string monitorPath; // UTF-8 device interface path, never an ordinal.
};
enum class ConfigStatus { Missing, Ok, Sanitized, Corrupt, Unsupported };
struct ParsedSettings { Settings settings; ConfigStatus status{ConfigStatus::Missing}; bool backupFailed{}; };
ParsedSettings parseSettings(const std::optional<std::string>& bytes);
std::string serializeSettings(const Settings& settings);
std::string canonicalPath(std::string path);
struct Identity { std::string path; bool selectable{true}; };
// Ambiguous, absent, or unsafe identities must never fall back to monitor 1.
using PathEqual = bool(*)(const std::string&, const std::string&);
bool asciiPathEqual(const std::string& a, const std::string& b);
std::optional<std::size_t> resolveMonitor(const std::string& path, const std::vector<Identity>& displays,
                                        PathEqual equal = asciiPathEqual);
struct SelectionRow {
    std::string path;
    std::optional<std::size_t> display;
    bool selectable{};
};
struct SelectionList { std::vector<SelectionRow> rows; std::size_t selected{}; };
// A missing draft stays visible but cannot be chosen as a different live target.
SelectionList selectionList(const std::string& draft, const std::vector<Identity>& displays,
                            PathEqual equal = asciiPathEqual);
// Move between safe rows without trapping keyboard focus on an unavailable row.
std::size_t nextSelectable(const SelectionList& list, std::size_t from, bool forward) noexcept;
enum class StartupState { Disabled, Current, Stale, Unreadable };
std::wstring startupCommand(const std::wstring& executable);
using WideEqual = bool(*)(const std::wstring&, const std::wstring&);
bool exactWideEqual(const std::wstring& a, const std::wstring& b);
StartupState classifyStartup(const std::optional<std::wstring>& value, const std::wstring& executable,
                             bool readable, WideEqual equal = exactWideEqual);
unsigned pollInterval(State state, bool blocked) noexcept;
inline constexpr std::uint64_t ActivationProtocol = 0x4f4c0201;
inline constexpr std::uint64_t ActivationProbe = 1, ActivationShow = 2;
inline constexpr std::uint64_t ActivationReply = 0x4f4c4143;
constexpr bool validActivation(std::uint64_t action, std::uint64_t protocol) noexcept {
    return protocol == ActivationProtocol && (action == ActivationProbe || action == ActivationShow);
}
} // namespace oled

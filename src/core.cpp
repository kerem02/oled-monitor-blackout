#include "core.hpp"
#include <charconv>
#include <map>
#include <sstream>
#include <stdexcept>

namespace oled {
const char* stateName(State s) noexcept {
    switch (s) {
    case State::Disabled: return "Disabled";
    case State::Unavailable: return "Selected display unavailable";
    case State::Paused: return "Paused";
    case State::OnDisplay: return "Cursor on OLED";
    case State::Waiting: return "Waiting";
    case State::BlackedOut: return "Blacked out";
    }
    return "Unknown";
}
DisplayPowerTransition displayPowerTransition(bool wasOff, unsigned reportedState) noexcept {
    // GUID_CONSOLE_DISPLAY_STATE reports 0=off, 1=on and 2=dimmed. Repeated
    // notifications and on<->dimmed changes are not topology changes.
    if (reportedState > 2) return DisplayPowerTransition::None;
    const bool isOff = reportedState == 0;
    if (isOff == wasOff) return DisplayPowerTransition::None;
    return isOff ? DisplayPowerTransition::BecameOff : DisplayPowerTransition::BecameOn;
}
State StateMachine::step(const Input& in) noexcept {
    State next;
    if (!in.enabled) next = State::Disabled;
    else if (!in.available || !in.bounds.valid()) next = State::Unavailable;
    else if (in.paused || !in.cursorValid) next = State::Paused;
    else if (in.bounds.contains(in.cursor)) next = State::OnDisplay;
    else {
        if (!awaySince_ || in.nowMs < *awaySince_) awaySince_ = in.nowMs;
        const unsigned delay = in.delaySeconds >= 5 && in.delaySeconds <= 3600 ? in.delaySeconds : 30;
        next = in.nowMs - *awaySince_ >= static_cast<std::uint64_t>(delay) * 1000
            ? State::BlackedOut : State::Waiting;
    }
    if (next != State::Waiting && next != State::BlackedOut) awaySince_.reset();
    return state_ = next;
}
std::string canonicalPath(std::string path) {
    for (char& c : path) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return path;
}
bool asciiPathEqual(const std::string& a, const std::string& b) { return canonicalPath(a) == canonicalPath(b); }
std::optional<std::size_t> resolveMonitor(const std::string& path, const std::vector<Identity>& displays, PathEqual equal) {
    if (path.empty()) return std::nullopt;
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < displays.size(); ++i) {
        if (equal(displays[i].path, path)) {
            if (found || !displays[i].selectable) return std::nullopt;
            found = i;
        }
    }
    return found;
}

SelectionList selectionList(const std::string& draft, const std::vector<Identity>& displays, PathEqual equal) {
    SelectionList list;
    list.rows.push_back({"", std::nullopt, true}); // Explicit clear-selection action.
    const auto match = resolveMonitor(draft, displays, equal);
    for (std::size_t i = 0; i < displays.size(); ++i) {
        const auto unique = resolveMonitor(displays[i].path, displays, equal);
        list.rows.push_back({displays[i].path, i, unique.has_value()});
    }
    if (match) list.selected = *match + 1;
    else if (!draft.empty()) {
        list.selected = list.rows.size();
        list.rows.push_back({draft, std::nullopt, false});
    }
    return list;
}
std::size_t nextSelectable(const SelectionList& list, std::size_t from, bool forward) noexcept {
    if (from >= list.rows.size()) return list.selected;
    auto i = from;
    while (forward ? i + 1 < list.rows.size() : i > 0) {
        if (forward) ++i; else --i;
        if (list.rows[i].selectable) return i;
    }
    return from;
}
std::wstring startupCommand(const std::wstring& executable) {
    if (executable.empty() || executable.find_first_of(L"\"\r\n") != std::wstring::npos ||
        executable.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("Invalid startup executable path");
    return L"\"" + executable + L"\" --startup";
}
bool exactWideEqual(const std::wstring& a, const std::wstring& b) { return a == b; }
StartupState classifyStartup(const std::optional<std::wstring>& value, const std::wstring& executable,
                             bool readable, WideEqual equal) {
    if (!readable) return StartupState::Unreadable;
    if (!value) return StartupState::Disabled;
    const auto expected = startupCommand(executable);
    const auto quotedSize = executable.size() + 2;
    return value->size() == expected.size() && value->substr(quotedSize) == L" --startup" &&
        equal(value->substr(0, quotedSize), expected.substr(0, quotedSize)) ? StartupState::Current : StartupState::Stale;
}
unsigned pollInterval(State state, bool blocked) noexcept {
    if (blocked || state == State::Disabled || state == State::Unavailable) return 1000;
    return state == State::BlackedOut ? 50 : 100;
}

static bool number(std::string_view value, unsigned& out) {
    if (value.empty()) return false;
    auto result = std::from_chars(value.data(), value.data() + value.size(), out);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}
ParsedSettings parseSettings(const std::optional<std::string>& bytes) {
    if (!bytes) return {};
    const auto bad = [](ConfigStatus s) { return ParsedSettings{Settings{}, s}; };
    if (bytes->size() > 16384 || bytes->find('\0') != std::string::npos) return bad(ConfigStatus::Corrupt);
    std::map<std::string, std::string> fields;
    std::istringstream stream(*bytes);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0 || !fields.emplace(line.substr(0, eq), line.substr(eq + 1)).second)
            return bad(ConfigStatus::Corrupt);
    }
    unsigned version{};
    if (!number(fields["version"], version)) return bad(ConfigStatus::Corrupt);
    if (version != 1) return bad(ConfigStatus::Unsupported);
    for (auto key : {"enabled", "delay_seconds", "hotkey_enabled", "monitor_path"})
        if (!fields.count(key)) return bad(ConfigStatus::Corrupt);
    if ((fields["enabled"] != "0" && fields["enabled"] != "1") ||
        (fields["hotkey_enabled"] != "0" && fields["hotkey_enabled"] != "1")) return bad(ConfigStatus::Corrupt);
    ParsedSettings result{{}, ConfigStatus::Ok};
    result.settings.enabled = fields["enabled"] == "1";
    result.settings.hotkeyEnabled = fields["hotkey_enabled"] == "1";
    unsigned delay{};
    if (!number(fields["delay_seconds"], delay) || delay < 5 || delay > 3600)
        result.status = ConfigStatus::Sanitized;
    else result.settings.delaySeconds = delay;
    const auto& path = fields["monitor_path"];
    if (path.size() > 2048) return bad(ConfigStatus::Corrupt);
    for (unsigned char c : path) if (c < 32 || c == 127) return bad(ConfigStatus::Corrupt);
    result.settings.monitorPath = canonicalPath(path);
    return result;
}
std::string serializeSettings(const Settings& s) {
    return "version=1\nenabled=" + std::to_string(s.enabled) + "\ndelay_seconds=" + std::to_string(s.delaySeconds) +
        "\nhotkey_enabled=" + std::to_string(s.hotkeyEnabled) + "\nmonitor_path=" + canonicalPath(s.monitorPath) + "\n";
}
} // namespace oled

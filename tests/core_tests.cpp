#include "core.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <functional>
using namespace oled;
namespace {
int assertions{};
void check(bool condition, const char* expression, int line) {
    ++assertions;
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
Input ready() { return {true, true, false, true, {-1920, -1080, 0, 0}, {-100, -100}, 0, 5}; }
void geometry() {
    Rect r{-1920, -1080, 0, 0};
    CHECK(r.contains({-1920, -1080})); CHECK(r.contains({-1, -1}));
    CHECK(!r.contains({0, -1})); CHECK(!r.contains({-1, 0}));
    CHECK(!r.contains({-1921, -1})); CHECK(!r.contains({-1, -1081}));
    CHECK(!Rect{}.contains({0, 0})); CHECK(!Rect{0, 0, -10, 10}.valid());
    const std::vector<Rect> displays{{-1920,0,0,1080},{0,0,2560,1440},{0,-2160,3840,0},{2560,0,3640,1920}};
    CHECK(displays[1].contains({0,0})); CHECK(!displays[0].contains({0,0}));
    CHECK(displays[2].contains({3839,-1})); CHECK(displays[3].contains({2560,1919}));
    CHECK(!displays[3].contains({3640,1919}));
}
void transitions() {
    StateMachine m; auto in = ready();
    CHECK(m.step(in) == State::OnDisplay);
    in.cursor = {500, 100}; in.nowMs = 1000;
    CHECK(m.step(in) == State::Waiting);
    in.nowMs = 5999; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 6000; CHECK(m.step(in) == State::BlackedOut);
    in.nowMs = 9000; CHECK(m.step(in) == State::BlackedOut);
    in.cursor = {-1,-1}; CHECK(m.step(in) == State::OnDisplay);
    in.cursor = {500,100}; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 14000; CHECK(m.step(in) == State::BlackedOut);
    in.enabled = false; CHECK(m.step(in) == State::Disabled);
    in.enabled = true; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 19000; CHECK(m.step(in) == State::BlackedOut);
    in.available = false; CHECK(m.step(in) == State::Unavailable);
    in.available = true; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 24000; CHECK(m.step(in) == State::BlackedOut);
    in.cursorValid = false; CHECK(m.step(in) == State::Paused);
    in.cursorValid = true; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 29000; CHECK(m.step(in) == State::BlackedOut);
    in.paused = true; CHECK(m.step(in) == State::Paused);
    in.paused = false; CHECK(m.step(in) == State::Waiting);
    m.reset(); in.nowMs = 99999; CHECK(m.step(in) == State::Waiting);
    in.bounds = {}; CHECK(m.step(in) == State::Unavailable);
}
void displayPowerTransitions() {
    using T = DisplayPowerTransition;
    CHECK(displayPowerTransition(false,0) == T::BecameOff);
    CHECK(displayPowerTransition(true,0) == T::None);
    CHECK(displayPowerTransition(true,1) == T::BecameOn);
    CHECK(displayPowerTransition(true,2) == T::BecameOn);
    CHECK(displayPowerTransition(false,1) == T::None);
    CHECK(displayPowerTransition(false,2) == T::None);
    CHECK(displayPowerTransition(false,3) == T::None);
    CHECK(displayPowerTransition(true,99) == T::None);
}
void timeEdges() {
    auto in = ready(); in.cursor = {100,100}; StateMachine m;
    in.nowMs = 0xFFFFFFFFull - 1000; CHECK(m.step(in) == State::Waiting);
    in.nowMs += 5000; CHECK(m.step(in) == State::BlackedOut);
    m.reset(); in.nowMs = 50000; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 100; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 5099; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 5100; CHECK(m.step(in) == State::BlackedOut);
    m.reset(); in.nowMs = std::numeric_limits<std::uint64_t>::max() - 1000;
    CHECK(m.step(in) == State::Waiting);
    in.nowMs = 1; CHECK(m.step(in) == State::Waiting);
    m.reset(); in.nowMs = 0; in.delaySeconds = 0; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 29999; CHECK(m.step(in) == State::Waiting);
    in.nowMs = 30000; CHECK(m.step(in) == State::BlackedOut);
}
void identities() {
    std::vector<Identity> ids{{"path-b",true},{"PATH-A",true},{"path-c",false}};
    CHECK(resolveMonitor("path-a", ids) == 1);
    std::swap(ids[0], ids[1]); CHECK(resolveMonitor("PATH-A",ids) == 0);
    CHECK(!resolveMonitor("missing",ids)); CHECK(!resolveMonitor("",ids));
    CHECK(!resolveMonitor("path-c",ids));
    ids.push_back({"Path-a",true}); CHECK(!resolveMonitor("path-a",ids));
    ids.back().selectable = false; CHECK(!resolveMonitor("path-a",ids));
    ids.erase(ids.begin()); CHECK(!resolveMonitor("path-a",ids));
}
void configuration() {
    CHECK(parseSettings(std::nullopt).status == ConfigStatus::Missing);
    CHECK(parseSettings("").status == ConfigStatus::Corrupt);
    Settings s; s.monitorPath = R"(\\?\DISPLAY#LG#ABC#{GUID})"; s.delaySeconds = 3600; s.enabled = false;
    auto text = serializeSettings(s); auto result = parseSettings(text);
    CHECK(result.status == ConfigStatus::Ok); CHECK(result.settings.delaySeconds == 3600);
    CHECK(!result.settings.enabled); CHECK(result.settings.monitorPath == canonicalPath(s.monitorPath));
    CHECK(parseSettings(serializeSettings(Settings{})).settings.monitorPath.empty());
    CHECK(parseSettings("version=99\n").status == ConfigStatus::Unsupported);
    CHECK(parseSettings("version=one\n").status == ConfigStatus::Corrupt);
    CHECK(parseSettings("version=1\n").status == ConfigStatus::Corrupt);
    CHECK(parseSettings(text + "enabled=0\n").status == ConfigStatus::Corrupt);
    CHECK(parseSettings(text + "nonsense\n").status == ConfigStatus::Corrupt);
    CHECK(parseSettings(std::string(17000,'x')).status == ConfigStatus::Corrupt);
    CHECK(parseSettings(text + std::string(1,'\0')).status == ConfigStatus::Corrupt);
    for (const auto& invalid : {"0", "-1", "4", "3601", "999999999999999999", "5x", "", "5.1", " 5"}) {
        const std::string bytes = "version=1\nenabled=1\nhotkey_enabled=1\nmonitor_path=abc\ndelay_seconds=" + std::string(invalid) + "\n";
        result = parseSettings(bytes); CHECK(result.status == ConfigStatus::Sanitized);
        CHECK(result.settings.delaySeconds == 30); CHECK(result.settings.monitorPath == "abc");
    }
    for (unsigned delay : {5u,10u,30u,60u,120u,3600u}) { s.delaySeconds = delay; CHECK(parseSettings(serializeSettings(s)).settings.delaySeconds == delay); }
    auto crlf = text; std::size_t pos = 0;
    while ((pos = crlf.find('\n',pos)) != std::string::npos) { crlf.insert(pos,"\r"); pos += 2; }
    CHECK(parseSettings(crlf).status == ConfigStatus::Ok);
    text.replace(text.find("enabled=0"), 9, "enabled=2"); CHECK(parseSettings(text).status == ConfigStatus::Corrupt);
}
void startupLogic() {
    const std::wstring path = L"C:\\Tools With Spaces\\OLED Blackout\\OLEDBlackout.exe";
    CHECK(startupCommand(path) == L"\"C:\\Tools With Spaces\\OLED Blackout\\OLEDBlackout.exe\" --startup");
    CHECK(classifyStartup(std::nullopt,path,true) == StartupState::Disabled);
    CHECK(classifyStartup(startupCommand(path),path,true) == StartupState::Current);
    CHECK(classifyStartup(startupCommand(L"D:\\Old Copy\\OLEDBlackout.exe"),path,true) == StartupState::Stale);
    CHECK(classifyStartup(L"",path,true) == StartupState::Stale);
    CHECK(classifyStartup(path+L" --startup",path,true) == StartupState::Stale);
    CHECK(classifyStartup(startupCommand(path)+L" --extra",path,true) == StartupState::Stale);
    CHECK(classifyStartup(startupCommand(path),path,false) == StartupState::Unreadable);
    CHECK(classifyStartup(std::nullopt,path,false) == StartupState::Unreadable);
    CHECK(startupCommand(L"C:\\Türkçe\\OLED.exe").find(L"Türkçe") != std::wstring::npos);
    for (const std::wstring bad : {L"",L"C:\\bad\"name.exe",L"C:\\bad\nname.exe"}) {
        bool rejected=false; try { (void)startupCommand(bad); } catch (const std::invalid_argument&) { rejected=true; }
        CHECK(rejected);
    }
}
void selectionRefresh() {
    std::vector<Identity> displays{{"saved-a",true},{"unsaved-b",true},{"mirror",false}};
    auto list=selectionList("unsaved-b",displays);
    CHECK(list.selected==2); CHECK(list.rows[list.selected].path=="unsaved-b");
    std::swap(displays[0],displays[1]); list=selectionList("unsaved-b",displays);
    CHECK(list.selected==1); CHECK(list.rows[list.selected].display==0);
    displays.erase(displays.begin()); list=selectionList("unsaved-b",displays);
    CHECK(list.rows[list.selected].path=="unsaved-b"); CHECK(!list.rows[list.selected].selectable);
    CHECK(!list.rows[list.selected].display); // Unavailable draft retained; never selects saved-a.
    displays.push_back({"UNSAVED-B",true}); list=selectionList("unsaved-b",displays);
    CHECK(list.rows[list.selected].selectable); CHECK(list.rows[list.selected].display==2);
    displays.push_back({"unsaved-b",true}); list=selectionList("unsaved-b",displays);
    CHECK(!list.rows[list.selected].selectable); CHECK(!list.rows[3].selectable); CHECK(!list.rows[4].selectable);
    list=selectionList("",displays); CHECK(list.selected==0); CHECK(list.rows[0].path.empty()); CHECK(list.rows[0].selectable);
    CHECK(canonicalPath(u8"DISPLAY#ÅÄÜé#ABC") == u8"display#ÅÄÜé#abc"); // Non-ASCII bytes survive portable storage.
}
void keyboardSelection() {
    const auto list=selectionList("missing",{{"mirror",false},{"safe",true},{"duplicate",false}});
    CHECK(nextSelectable(list,0,true)==2);
    CHECK(nextSelectable(list,2,false)==0);
    CHECK(nextSelectable(list,2,true)==2);
    CHECK(nextSelectable(list,list.selected,false)==2);
    CHECK(nextSelectable(list,0,false)==0);
    CHECK(nextSelectable(list,999,true)==list.selected);
    CHECK(nextSelectable(SelectionList{},0,true)==0);
}
void geometryExtremes() {
    const int low=std::numeric_limits<int>::min(), high=std::numeric_limits<int>::max();
    Rect wide{low,0,high,1080}; CHECK(!wide.valid()); CHECK(wide.width()==0); CHECK(!wide.contains({0,0}));
    Rect negative{low,low,low+1920,low+1080}; CHECK(negative.valid()); CHECK(negative.contains({low,low}));
    CHECK(!negative.contains({low+1920,low})); CHECK(negative.width()==1920); CHECK(negative.height()==1080);
    Rect positive{high-3840,high-2160,high,high}; CHECK(positive.valid()); CHECK(positive.contains({high-1,high-1}));
    CHECK(!positive.contains({high,high-1}));
}
void integrationPolicies() {
    CHECK(pollInterval(State::BlackedOut,false)==50);
    CHECK(pollInterval(State::OnDisplay,false)==100);
    CHECK(pollInterval(State::Waiting,false)==100);
    CHECK(pollInterval(State::Disabled,false)==1000);
    CHECK(pollInterval(State::Unavailable,false)==1000);
    CHECK(pollInterval(State::BlackedOut,true)==1000);
    CHECK(validActivation(ActivationProbe,ActivationProtocol)); CHECK(validActivation(ActivationShow,ActivationProtocol));
    CHECK(!validActivation(0,ActivationProtocol)); CHECK(!validActivation(3,ActivationProtocol));
    CHECK(!validActivation(ActivationShow,0)); CHECK(!validActivation(ActivationProbe,ActivationProtocol+1));
    StateMachine m; auto in=ready(); in.cursor={1,1}; m.step(in); in.nowMs=5000; CHECK(m.step(in)==State::BlackedOut);
    in.paused=true; CHECK(m.step(in)==State::Paused); in.paused=false; in.nowMs=9000; CHECK(m.step(in)==State::Waiting);
    in.nowMs=13999; CHECK(m.step(in)==State::Waiting); in.nowMs=14000; CHECK(m.step(in)==State::BlackedOut);
    m.reset(); CHECK(m.step(in)==State::Waiting); // Timer/refresh changes cannot retain an elapsed countdown.
}
void safetyProperties() {
    std::mt19937 random(20260916); StateMachine m; auto in = ready();
    for (int i = 0; i < 100000; ++i) {
        in.nowMs += random() % 10000;
        in.enabled = random() % 5 != 0; in.available = random() % 7 != 0;
        in.paused = random() % 11 == 0; in.cursorValid = random() % 13 != 0;
        in.cursor = random() % 2 ? Point{-100,-100} : Point{100,100};
        const State state = m.step(in);
        if (state == State::BlackedOut) CHECK(in.enabled && in.available && !in.paused && in.cursorValid && !in.bounds.contains(in.cursor));
        if (!in.enabled) CHECK(state == State::Disabled);
        if (in.enabled && !in.available) CHECK(state == State::Unavailable);
        if (in.enabled && in.available && (in.paused || !in.cursorValid)) CHECK(state == State::Paused);
        if (in.enabled && in.available && !in.paused && in.cursorValid && in.bounds.contains(in.cursor)) CHECK(state == State::OnDisplay);
    }
    for (int i = 0; i < 10000; ++i) {
        std::string data;
        for (unsigned j = 0, count = static_cast<unsigned>(random()%512); j < count; ++j) data += static_cast<char>(random()%256);
        const auto parsed = parseSettings(data);
        CHECK(parsed.settings.delaySeconds >= 5 && parsed.settings.delaySeconds <= 3600);
        if (parsed.status == ConfigStatus::Corrupt || parsed.status == ConfigStatus::Unsupported) CHECK(parsed.settings.monitorPath.empty());
    }
}
}
int main() {
    const std::pair<const char*,std::function<void()>> tests[] = {
        {"geometry",geometry},{"state transitions",transitions},{"display power transitions",displayPowerTransitions},
        {"monotonic time edges",timeEdges},
        {"identity reorder/ambiguity",identities},{"configuration",configuration},{"startup command/state",startupLogic},
        {"unsaved selection refresh",selectionRefresh},{"keyboard selection",keyboardSelection},{"extreme signed geometry",geometryExtremes},
        {"polling and activation policies",integrationPolicies},{"randomized safety properties",safetyProperties}};
    try { for (const auto& test : tests) { test.second(); std::cout << "PASS " << test.first << '\n'; } }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
    std::cout << assertions << " assertions passed\n";
}

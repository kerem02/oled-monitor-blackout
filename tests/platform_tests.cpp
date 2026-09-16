#include "platform.hpp"
#include "instance.hpp"
#include <fstream>
#include <iostream>
using namespace oled;
namespace {
unsigned assertions{};
void require(bool value, const char* why) { ++assertions; if (!value) throw std::runtime_error(why); }
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc); stream << bytes;
    if (!stream) throw std::runtime_error("Test fixture write failed");
}
void startupTests() {
    const auto keyPath=L"Software\\OLED Blackout\\IntegrationTests-"+std::to_wstring(GetCurrentProcessId());
    HKEY key{};
    require(RegCreateKeyExW(HKEY_CURRENT_USER,keyPath.c_str(),0,nullptr,REG_OPTION_VOLATILE,KEY_ALL_ACCESS,nullptr,&key,nullptr)==ERROR_SUCCESS,"create isolated registry fixture");
    struct Cleanup {
        HKEY key; std::wstring path;
        ~Cleanup() { RegCloseKey(key); RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str()); }
    } cleanup{key,keyPath};
    const std::wstring here=L"C:\\Portable Apps\\Türkçe\\OLED.exe";
    StartupRegistration registration(HKEY_CURRENT_USER,keyPath,here);
    require(registration.read().state==StartupState::Disabled,"startup missing");
    registration.set(true); require(registration.read().state==StartupState::Current,"startup quoted path with spaces");
    const std::wstring old=L"D:\\Old Folder\\OLED.exe";
    StartupRegistration stale(HKEY_CURRENT_USER,keyPath,old);
    require(stale.read().state==StartupState::Stale,"startup stale path detected");
    const DWORD marker=42;
    require(RegSetValueExW(key,L"Unrelated",0,REG_DWORD,reinterpret_cast<const BYTE*>(&marker),sizeof(marker))==ERROR_SUCCESS,"unrelated value fixture");
    stale.set(false); require(stale.read().state==StartupState::Disabled,"unchecked removes stale entry");
    registration.set(true); stale.set(true); require(stale.read().state==StartupState::Current,"checked repairs stale entry");
    require(registration.read().state==StartupState::Stale,"old registration sees new location");
    require(RegSetValueExW(key,L"OLED Blackout",0,REG_DWORD,reinterpret_cast<const BYTE*>(&marker),sizeof(marker))==ERROR_SUCCESS,"malformed type fixture");
    require(registration.read().state==StartupState::Unreadable,"unreadable startup type reported");
    registration.set(false); require(registration.read().state==StartupState::Disabled,"unreadable entry can be removed");
    DWORD preserved{},size=sizeof(preserved);
    require(RegGetValueW(key,nullptr,L"Unrelated",RRF_RT_REG_DWORD,nullptr,&preserved,&size)==ERROR_SUCCESS && preserved==marker,"unrelated Run values preserved");
    require(windowsOrdinalEqual(L"DISPLAY#ÅÜ",L"display#åü"),"Windows ordinal non-ASCII case matching");
    require(!windowsOrdinalEqual(L"é",L"e\u0301"),"no linguistic normalization of identity");
    require(resolveMonitor(u8"DISPLAY#Å",{{u8"display#å",true}},devicePathEqual)==0,"Unicode path resolution");
    require(!resolveMonitor(u8"DISPLAY#Å",{{u8"display#å",true},{u8"DISPLAY#Å",true}},devicePathEqual),"Unicode duplicate identity rejected");
    require(classifyStartup(startupCommand(L"c:\\portable apps\\türkçe\\oled.exe"),here,true,windowsOrdinalEqual)==StartupState::Current,"startup path case independent");
}
LRESULT CALLBACK testController(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    static UINT activation=RegisterWindowMessageW(ActivationMessageName);
    if (message==activation)
        return validActivation(wp,static_cast<std::uint64_t>(lp)) && isActivationTarget(window) ? static_cast<LRESULT>(ActivationReply) : 0;
    return DefWindowProcW(window,message,wp,lp);
}
void activationTests() {
    WNDCLASSW wc{}; wc.lpfnWndProc=testController; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=ControllerClass;
    require(RegisterClassW(&wc)!=0,"activation test class");
    HWND window=CreateWindowExW(0,ControllerClass,ControllerTitle,WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr);
    require(window!=nullptr,"activation test controller");
    struct Cleanup { HWND window; HINSTANCE instance; ~Cleanup() { DestroyWindow(window); UnregisterClassW(ControllerClass,instance); } } cleanup{window,wc.hInstance};
    require(!isActivationTarget(nullptr),"null activation target rejected");
    require(!isActivationTarget(window),"controller without protocol marker rejected");
    require(SetPropW(window,ActivationProperty,reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(ActivationProtocol)))!=FALSE,"activation protocol property");
    require(isActivationTarget(window),"controller class title marker accepted");
    const UINT message=RegisterWindowMessageW(ActivationMessageName);
    require(SendMessageW(window,message,ActivationProbe,ActivationProtocol)==static_cast<LRESULT>(ActivationReply),"activation handshake");
    require(SendMessageW(window,message,ActivationProbe,ActivationProtocol+1)==0,"bad protocol rejected");
    require(SendMessageW(window,message,7,ActivationProtocol)==0,"unknown action rejected");
    SetWindowTextW(window,L"Different window"); require(!isActivationTarget(window),"wrong title rejected");
    // Do not call activateExistingInstance: it could target a user's running application.
}

}
int main() {
    std::filesystem::path dir;
    try {
        dir = std::filesystem::temp_directory_path() / (L"OLED-Blackout-Test-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directory(dir);
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{dir};
        Log log(dir); SettingsStore store(dir, log);
        require(store.load().status == ConfigStatus::Missing, "missing file");
        Settings first; first.monitorPath = "display-test"; store.save(first);
        require(store.load().settings.monitorPath == "display-test", "persist identity");
        require(!std::filesystem::exists(dir / L"settings.tmp"), "temp file consumed");
        Settings second = first; second.delaySeconds = 120; store.save(second);
        require(store.load().settings.delaySeconds == 120, "replace saved settings");
        {
            Handle lock(CreateFileW((dir / L"settings.ini").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
            require(lock.get() != INVALID_HANDLE_VALUE, "fixture read lock");
            bool failed = false;
            try { store.save(first); } catch (const std::exception&) { failed = true; }
            require(failed, "replacement failure must be reported");
            require(store.load().settings.delaySeconds == 120, "failed replacement must preserve previous config");
        }
        store.save(first); require(store.load().settings.delaySeconds == 30, "recover after write failure");
        write(dir / L"settings.ini", "corrupt!");
        require(store.load().status == ConfigStatus::Corrupt, "corrupt detection");
        require(std::filesystem::exists(dir / L"settings.recovered.ini"), "corrupt backup");
        require(store.load().settings.monitorPath.empty(), "corrupt config fails open");
        write(dir / L"settings.ini", "version=2\n");
        require(store.load().status == ConfigStatus::Unsupported, "future schema detection");
        write(dir / L"settings.ini", std::string(20000, 'x'));
        require(store.load().status == ConfigStatus::Corrupt, "oversized settings");
        std::string badUtf8 = serializeSettings(first); badUtf8 += "#";
        badUtf8.insert(badUtf8.find("display-test"), 1, static_cast<char>(0xff));
        write(dir / L"settings.ini", badUtf8);
        require(store.load().status == ConfigStatus::Corrupt, "invalid UTF-8 identity");
        write(dir / L"app.log", std::string(256*1024, 'x'));
        log.write("rotation test");
        require(std::filesystem::exists(dir / L"app.previous.log"), "log rotates");
        require(std::filesystem::file_size(dir / L"app.log") < 1024, "new log bounded");
        std::filesystem::remove(dir/L"settings.recovered.ini");
        std::filesystem::create_directory(dir/L"settings.recovered.ini"); // Force backup creation to fail.
        write(dir/L"settings.ini","broken");
        auto recovered=store.load();
        require(recovered.status==ConfigStatus::Corrupt && recovered.backupFailed,"backup failure reported without aborting recovery");
        require(recovered.settings.monitorPath.empty() && recovered.settings.delaySeconds==30,"backup failure retains safe defaults");
        write(dir/L"settings.ini","version=99\n"); recovered=store.load();
        require(recovered.status==ConfigStatus::Unsupported && recovered.backupFailed && recovered.settings.monitorPath.empty(),"future config backup failure fails open");
        startupTests(); activationTests();
        require(windowsErrorText("Write settings",ERROR_ACCESS_DENIED).find("5")!=std::string::npos,"human-readable errors retain numeric code");
        const std::string unicode = u8"Türkçe – OLED";
        require(utf8(wide(unicode)) == unicode, "UTF-8 round trip");
        std::cout << "PASS Windows persistence, backup-failure recovery, isolated startup registry, Unicode identity, activation validation, log rotation\n"
            << assertions << " assertions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}

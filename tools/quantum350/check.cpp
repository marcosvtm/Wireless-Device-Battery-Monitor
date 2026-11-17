#include <shlobj.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <shlwapi.h>
HRESULT FakeFolder(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR* path) {
    return SHStrDupW(std::filesystem::current_path().c_str(), path);
}
#define SHGetKnownFolderPath FakeFolder
#include "reports.cpp"
#define Describe FakeDescribe
#define Sample FakeSample
Probe FakeDescribe(FILE*, const HidColl&);
void FakeSample(FILE*, const Probe&, ULONGLONG);
#include "capture.cpp"
#undef SHGetKnownFolderPath
bool simulate = true;
std::vector<HidColl> EnumHid() {
    if (!simulate) return {};
    HidColl control;
    control.vid = 0x0ecb;
    control.pid = 0x206b;
    control.iface = 3;
    control.caps.UsagePage = 1;
    control.caps.InputReportByteLength = 2;
    control.caps.FeatureReportByteLength = 64;
    HidColl media = control;
    media.caps.UsagePage = 0x0c;
    media.caps.Usage = 1;
    media.caps.InputReportByteLength = 3;
    media.caps.FeatureReportByteLength = 0;
    HidColl unrelated = control, keyboard = control, otherInterface = control;
    unrelated.pid = 0x206c;
    keyboard.caps.Usage = 6;
    otherInterface.iface = 4;
    return {control, media, unrelated, keyboard, otherInterface};
}
int snapshots;
Probe FakeDescribe(FILE* log, const HidColl& c) {
    Probe p{c, {}};
    HIDP_VALUE_CAPS cap{};
    cap.UsagePage = 0xff00;
    cap.ReportID = 0x61; // serial, excluded from requests
    RecordCaps(log, p, HidP_Feature, cap, 8, 1);
    assert(p.requests.empty());
    if (c.featLen()) {
        cap.ReportID = 0x49;
        RecordCaps(log, p, HidP_Feature, cap, 8, 1);
        RecordCaps(log, p, HidP_Feature, cap, 8, 1);
        assert(p.requests.size() == 1);
    }
    return p;
}
void FakeSample(FILE*, const Probe& p, ULONGLONG) {
    assert(p.requests.size() == 1 && p.requests.front().id == 0x49);
    ++snapshots;
}
bool HidDev::OpenReadOnly(const HidColl& c) {
    assert(c.pid == 0x206b && c.iface == 3);
    c_ = c;
    return true;
}
void HidDev::Close() {}
int HidDev::Read(std::vector<BYTE>& b, int) { b = {7, 75}; return 2; }
std::wstring label(HWND w, int id) {
    wchar_t s[256];
    GetDlgItemTextW(w, id, s, 256);
    return s;
}
int main() {
    for (bool attached : {true, false}) {
        simulate = attached;
        Test t;
        HWND w = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(DIALOG_TEST),
            nullptr, Dialog, reinterpret_cast<LPARAM>(&t));
        assert(w && label(w, ACTION) == L"Começar");
        auto click = [&] { SendMessageW(w, WM_COMMAND, ACTION, 0); };
        SetDlgItemTextW(w, PERCENTAGE, L"101");
        click();
        assert(!t.log && !t.start && label(w, STATUS_LINE).find(L"0 a 100") != std::wstring::npos);
        SetDlgItemTextW(w, PERCENTAGE, L"75");
        click();
        assert(t.start && !t.path.empty());
        if (attached) {
            assert(t.log && t.matches == 2 && t.readers.size() == 2 && t.probes.size() == 1 && !IsWindowEnabled(GetDlgItem(w, ACTION)));
            auto start = t.start;
            t.start = start - 11000;
            Tick(w, t);
            assert(IsWindowEnabled(GetDlgItem(w, ACTION)) && label(w, ACTION) == L"Conectei");
            click();
            t.start = start - 36000;
            Tick(w, t);
            assert(IsWindowEnabled(GetDlgItem(w, ACTION)) && label(w, ACTION) == L"Desconectei");
            click();
            t.start = start - 60001;
            Tick(w, t);
        }
        assert(!t.log && label(w, ACTION) == L"Mostrar arquivo");
        std::ifstream in{std::filesystem::path(t.path)};
        std::string log((std::istreambuf_iterator<char>(in)), {});
        assert(log.find("QuantumENGINE: 75%") != log.npos);
        if (attached) {
            assert(log.find("user marked charging cable connected") != log.npos);
            assert(log.find("user marked charging cable disconnected") != log.npos);
            assert(log.find("usage 0001:0000 input 07 4b") != log.npos);
            assert(log.find("usage 000c:0001 input 07 4b") != log.npos);
            assert(snapshots == 3);
        } else {
            assert(log.find("Received 0 input reports.") != log.npos);
            assert(label(w, INSTRUCTIONS).find(L"não foi encontrado") != std::wstring::npos);
        }
        DestroyWindow(w);
        Sleep(5);
        std::cout << (attached ? "Simulated receiver and cable markers OK" : "Missing receiver guidance and log OK") << std::endl;
    }
}

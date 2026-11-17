#include "hid.h"
#include <commdlg.h>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <set>
#include <utility>

// A separate process records raw vendor input while the normal tray stays responsive.
int Quantum350Capture(HWND owner) {
    constexpr auto title = L"JBL Quantum 350 receiver test";
    if (MessageBoxW(owner, L"Turn on the Quantum 350 and connect its dongle. Note the level in QuantumENGINE. After choosing a log file, connect and disconnect the charging cable during the 60-second recording.", title, MB_OKCANCEL) != IDOK) return 0;
    wchar_t path[MAX_PATH] = L"Quantum350-test.txt";
    OPENFILENAMEW dlg{sizeof(dlg)};
    dlg.hwndOwner = owner;
    dlg.lpstrFilter = L"Text logs\0*.txt\0";
    dlg.lpstrFile = path;
    dlg.nMaxFile = MAX_PATH;
    dlg.lpstrDefExt = L"txt";
    dlg.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dlg)) return 0;
    FILE* log = _wfopen(path, L"w");
    if (!log) { MessageBoxW(owner,L"Cannot write the selected file.",title,MB_ICONERROR); return 1; }
    auto all = EnumHid();
    std::set<std::pair<USHORT,USHORT>> models;
    for (const auto& c : all) {
        auto name = HidProduct(c);
        std::wstring normalized;
        for (auto ch : name) if (iswalnum(ch)) normalized += towlower(ch);
        if (normalized.find(L"quantum350") != std::wstring::npos) models.emplace(c.vid,c.pid);
    }
    std::vector<std::unique_ptr<HidDev>> inputs;
    std::fputs("Wireless Battery / Quantum 350 diagnostic v1\n",log);
    for (const auto& c : all) {
        bool matched = models.count({c.vid,c.pid}) != 0;
        if (!matched && c.vid != 0x0ecb) continue;
        std::fprintf(log,"USB %04x:%04x iface %d usage %04x:%04x input %u feature %u matched %d\n",
            c.vid,c.pid,c.iface,c.page(),c.usage(),c.inLen(),c.featLen(),matched);
        if (!matched || c.page() < 0xff00 || !c.inLen()) continue;
        auto dev = std::make_unique<HidDev>();
        if (dev->Open(c, GENERIC_READ)) inputs.push_back(std::move(dev));
        else std::fputs("Cannot open this collection for reading.\n",log);
    }
    const auto start = GetTickCount64();
    unsigned reports = 0;
    while (!inputs.empty() && GetTickCount64()-start < 60000 && reports < 500) {
        for (const auto& dev : inputs) {
            std::vector<BYTE> bytes;
            if (dev->Read(bytes,50) <= 0) continue;
            const auto& c = dev->coll();
            std::fprintf(log,"%llu ms iface %d usage %04x:%04x input",GetTickCount64()-start,c.iface,c.page(),c.usage());
            for (auto b : bytes) std::fprintf(log," %02x",b);
            std::fputc('\n',log);
            if (++reports == 500) break;
        }
    }
    std::fprintf(log,"Opened %zu vendor collections. Received %u reports.\n",inputs.size(),reports);
    bool failed = std::ferror(log) != 0;
    failed = std::fclose(log) != 0 || failed;
    MessageBoxW(owner,failed ? L"Could not finish writing the log." : L"Test log saved. Share it with the QuantumENGINE battery percentage and the charging changes you made.",title,MB_OK);
    return failed ? 1 : 0;
}

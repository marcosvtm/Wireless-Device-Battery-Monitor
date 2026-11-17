// GPL-3.0. Uses the HID layer and receiver matching from Wireless Battery q350-test1.
#include "reports.h"
#include "dialog.h"
#include <shlobj.h>
#include <cstdio>
#include <memory>

namespace {
constexpr UINT_PTR timer = 1;
constexpr unsigned duration = 60000;
struct Test {
    FILE* log = nullptr;
    std::wstring path;
    std::vector<std::unique_ptr<HidDev>> readers;
    std::vector<Probe> probes;
    unsigned matches = 0;
    ULONGLONG start = 0;
    int marks = 0;
    unsigned reports = 0;
    int lastSecond = -1;
};

bool OpenLog(Test& t, const std::wstring& directory) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t name[110];
    swprintf(name, 110, L"\\Quantum350-%04u-%02u-%02u_%02uh%02um%02us-%03u-%lu.txt",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds, GetCurrentProcessId());
    t.path = directory + name;
    t.log = _wfopen(t.path.c_str(), L"wx");
    return t.log != nullptr;
}

void FindReceiver(Test& t) {
    for (const auto& c : EnumHid()) {
        if (c.vid != 0x0ecb || c.pid != 0x206b || c.iface != 3) continue;
        fprintf(t.log, "USB %04x:%04x iface %d usage %04x:%04x input %u output %u feature %u\n",
            c.vid, c.pid, c.iface, c.page(), c.usage(), c.inLen(), c.outLen(), c.featLen());
        if (!((c.page() == 1 && c.usage() == 0) || (c.page() == 0x0c && c.usage() == 1))) continue;
        ++t.matches;
        auto probe = Describe(t.log, c);
        if (!probe.requests.empty()) t.probes.push_back(std::move(probe));
        if (!c.inLen()) continue;
        auto reader = std::make_unique<HidDev>();
        if (reader->OpenReadOnly(c)) t.readers.push_back(std::move(reader));
        else fprintf(t.log, "Input open failed: Windows error %lu\n", GetLastError());
    }
}

void Finish(HWND window, Test& t, bool interrupted) {
    KillTimer(window, timer);
    fprintf(t.log, "Matched %u collections. Opened %zu input collections. Received %u input reports. Elapsed %llu ms. %s\n",
        t.matches, t.readers.size(), t.reports, GetTickCount64() - t.start, interrupted ? "Stopped early." : "Finished.");
    bool failed = ferror(t.log) != 0;
    if (fclose(t.log) != 0) failed = true;
    t.log = nullptr;
    t.readers.clear();
    SetDlgItemTextW(window, INSTRUCTIONS, failed ? L"O Windows não conseguiu salvar o arquivo completo." :
        !t.matches ? L"O receptor do Quantum 350 não foi encontrado. Confira se ele está conectado e envie o arquivo." :
        L"Pronto! Envie o arquivo de texto para quem pediu o teste.");
    SetDlgItemTextW(window, STATUS_LINE, failed ? L"Confira o espaço disponível na pasta Downloads." :
        L"O arquivo está na pasta Downloads.");
    SetDlgItemTextW(window, ACTION, L"Mostrar arquivo");
    EnableWindow(GetDlgItem(window, ACTION), !failed);
}

void Tick(HWND window, Test& t) {
    auto elapsed = GetTickCount64() - t.start;
    if (elapsed >= duration) { Finish(window, t, false); return; }
    // Bound work per collection per tick while draining queued reports in bursts.
    for (auto& reader : t.readers) {
        for (int n = 0; n < 12; ++n) {
            std::vector<BYTE> bytes;
            if (reader->Read(bytes, 1) <= 0) break;
            const auto& c = reader->coll();
            fprintf(t.log, "%llu ms iface %d usage %04x:%04x input", GetTickCount64() - t.start,
                c.iface, c.page(), c.usage());
            for (auto b : bytes) fprintf(t.log, " %02x", b);
            fputc('\n', t.log);
            ++t.reports;
        }
    }
    int second = static_cast<int>(elapsed / 1000);
    if (second == t.lastSecond) return;
    if (second / 5 != t.lastSecond / 5 || t.lastSecond < 0)
        for (const auto& probe : t.probes) Sample(t.log, probe, t.start);
    t.lastSecond = second;
    wchar_t remaining[80];
    swprintf(remaining, 80, L"Gravando… faltam %d s.", 60 - second);
    SetDlgItemTextW(window, STATUS_LINE, remaining);
    if (second >= 10 && t.marks == 0) {
        SetDlgItemTextW(window, INSTRUCTIONS, L"Conecte o cabo de carga ao fone e clique em “Conectei”.");
        EnableWindow(GetDlgItem(window, ACTION), TRUE);
    } else if (second >= 35 && t.marks == 1) {
        SetDlgItemTextW(window, INSTRUCTIONS, L"Tire o cabo de carga do fone e clique em “Desconectei”. Deixe o receptor USB no PC.");
        EnableWindow(GetDlgItem(window, ACTION), TRUE);
    }
}

void Start(HWND window, Test& t) {
    BOOL valid = FALSE;
    UINT percent = GetDlgItemInt(window, PERCENTAGE, &valid, FALSE);
    if (!valid || percent > 100) {
        SetDlgItemTextW(window, STATUS_LINE, L"Digite a porcentagem de bateria de 0 a 100.");
        SetFocus(GetDlgItem(window, PERCENTAGE));
        return;
    }
    PWSTR downloads = nullptr;
    HRESULT folder = SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE, nullptr, &downloads);
    bool opened = SUCCEEDED(folder) && OpenLog(t, downloads);
    CoTaskMemFree(downloads);
    if (!opened) {
        SetDlgItemTextW(window, STATUS_LINE, L"Não foi possível criar o arquivo na pasta Downloads.");
        return;
    }
    fprintf(t.log, "Wireless Battery / Quantum 350 standalone capture v2\nQuantumENGINE: %u%% (user reference)\n", percent);
    FindReceiver(t);
    t.start = GetTickCount64();
    ShowWindow(GetDlgItem(window, PERCENTAGE), SW_HIDE);
    ShowWindow(GetDlgItem(window, PERCENT_LABEL), SW_HIDE);
    if (t.readers.empty() && t.probes.empty()) { Finish(window, t, false); return; }
    SetDlgItemTextW(window, INSTRUCTIONS, L"Mantenha o fone ligado e o cabo de carga desconectado. Aguarde o aviso.");
    SetDlgItemTextW(window, ACTION, L"Conectei");
    EnableWindow(GetDlgItem(window, ACTION), FALSE);
    if (!SetTimer(window, timer, 40, nullptr)) { Finish(window, t, true); return; }
    Tick(window, t);
}

INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(window, DWLP_USER, l);
        SendDlgItemMessageW(window, PERCENTAGE, EM_SETLIMITTEXT, 3, 0);
        return TRUE;
    }
    auto* t = reinterpret_cast<Test*>(GetWindowLongPtrW(window, DWLP_USER));
    if (!t) return FALSE;
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
        if (t->log) Finish(window, *t, true);
        EndDialog(window, 0);
        return TRUE;
    }
    if (message == WM_TIMER && w == timer && t->log) { Tick(window, *t); return TRUE; }
    if (message != WM_COMMAND || LOWORD(w) != ACTION) return FALSE;
    if (!t->log && !t->start) { Start(window, *t); return TRUE; }
    if (!t->log) {
        PIDLIST_ABSOLUTE file = ILCreateFromPathW(t->path.c_str());
        HRESULT result = file ? SHOpenFolderAndSelectItems(file, 0, nullptr, 0) : E_FAIL;
        ILFree(file);
        if (FAILED(result)) MessageBoxW(window, t->path.c_str(), L"Arquivo salvo em", MB_OK);
        return TRUE;
    }
    if (t->marks >= 2 || !IsWindowEnabled(GetDlgItem(window, ACTION))) return TRUE;
    fprintf(t->log, "%llu ms user marked charging cable %s\n", GetTickCount64() - t->start,
        t->marks == 0 ? "connected" : "disconnected");
    ++t->marks;
    SetDlgItemTextW(window, ACTION, t->marks == 1 ? L"Desconectei" : L"Aguarde");
    SetDlgItemTextW(window, INSTRUCTIONS, t->marks == 1 ?
        L"Deixe o cabo conectado até aparecer o próximo aviso." : L"Deixe o fone ligado até o teste terminar.");
    EnableWindow(GetDlgItem(window, ACTION), FALSE);
    return TRUE;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Test test;
    INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(DIALOG_TEST), nullptr, Dialog,
        reinterpret_cast<LPARAM>(&test));
    if (SUCCEEDED(init)) CoUninitialize();
    return result == -1 ? 1 : 0;
}

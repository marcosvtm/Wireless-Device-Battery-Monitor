#include "reports.h"
#include <hidclass.h>
#include <algorithm>

namespace {
const char* TypeName(HIDP_REPORT_TYPE type) {
    return type == HidP_Feature ? "feature" : "input";
}

template<class Cap>
void RecordCaps(FILE* log, Probe& p, HIDP_REPORT_TYPE type, const Cap& cap, unsigned bits, unsigned count) {
    USAGE first = cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage;
    USAGE last = cap.IsRange ? cap.Range.UsageMax : first;
    fprintf(log, "descriptor %s id %02x usage %04x:%04x-%04x bits %u count %u\n",
        TypeName(type), cap.ReportID, cap.UsagePage, first, last, bits, count);
    // Device Controls / Battery Strength, or Battery System / RelativeStateOfCharge.
    bool standard = (cap.UsagePage == 6 && first <= 0x20 && last >= 0x20) ||
                    (cap.UsagePage == 0x85 && first <= 0x64 && last >= 0x64);
    // HIDV1 battery IDs on related JBLs. Only try IDs advertised by this receiver.
    // https://github.com/Tymon3310/jbl-quantum-linux/blob/main/docs/QUANTUM610_PACKETS.md
    bool family = type == HidP_Feature ? cap.ReportID == 0x49 : cap.ReportID == 0x08;
    if (!standard && !family) return;
    for (const auto& r : p.requests) if (r.id == cap.ReportID && r.type == type) return;
    p.requests.push_back({cap.ReportID, type});
}
}

Probe Describe(FILE* log, const HidColl& c) {
    Probe p{c, {}};
    // QuantumENGINE 2.9.4 registers both getters on Q350TV1. A Windows filter
    // may service them even if individual IDs are absent from the parsed caps.
    if (c.page() == 1 && c.usage() == 0 && c.featLen() >= 2)
        p.requests = {{0x68, HidP_Feature}, {0x49, HidP_Feature}};
    HANDLE h = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { fprintf(log, "Descriptor open error %lu\n", GetLastError()); return p; }
    PHIDP_PREPARSED_DATA data = nullptr;
    if (HidD_GetPreparsedData(h, &data)) {
        for (const auto type : {HidP_Input, HidP_Feature}) {
            bool feature = type == HidP_Feature;
            USHORT n = feature ? c.caps.NumberFeatureValueCaps : c.caps.NumberInputValueCaps;
            std::vector<HIDP_VALUE_CAPS> values(n);
            if (n && HidP_GetValueCaps(type, values.data(), &n, data) == HIDP_STATUS_SUCCESS) {
                for (unsigned i = 0; i < n; ++i)
                    RecordCaps(log, p, type, values[i], values[i].BitSize, values[i].ReportCount);
            }
            n = feature ? c.caps.NumberFeatureButtonCaps : c.caps.NumberInputButtonCaps;
            std::vector<HIDP_BUTTON_CAPS> buttons(n);
            if (n && HidP_GetButtonCaps(type, buttons.data(), &n, data) == HIDP_STATUS_SUCCESS) {
                for (unsigned i = 0; i < n; ++i) RecordCaps(log, p, type, buttons[i], 1, 0);
            }
        }
        HidD_FreePreparsedData(data);
    } else fprintf(log, "Descriptor read error %lu\n", GetLastError());
    CloseHandle(h);
    return p;
}

void Sample(FILE* log, const Probe& probe, ULONGLONG start) {
    const auto& c = probe.collection;
    HANDLE h = CreateFileW(c.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        h = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) { fprintf(log, "Snapshot open error %lu\n", GetLastError()); return; }
    for (const auto& r : probe.requests) {
        bool feature = r.type == HidP_Feature;
        bool q350Byte = feature && (r.id == 0x49 || r.id == 0x68);
        std::vector<BYTE> bytes(q350Byte ? 2 : feature ? c.featLen() : c.inLen(), 0xff);
        if (bytes.empty()) continue;
        bytes[0] = r.id;
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) break;
        DWORD n = 0;
        DWORD code = feature ? IOCTL_HID_GET_FEATURE : IOCTL_HID_GET_INPUT_REPORT;
        BOOL ok = DeviceIoControl(h, code, bytes.data(), (DWORD)bytes.size(), bytes.data(),
            (DWORD)bytes.size(), &n, &ov);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && error == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, 200) != WAIT_OBJECT_0) CancelIoEx(h, &ov);
            ok = GetOverlappedResult(h, &ov, &n, TRUE);
            error = ok ? ERROR_SUCCESS : GetLastError();
        }
        CloseHandle(ov.hEvent);
        fprintf(log, "%llu ms usage %04x:%04x GET %s %02x", GetTickCount64() - start,
            c.page(), c.usage(), TypeName(r.type), r.id);
        if (ok) {
            size_t length = std::min(bytes.size(), size_t(n) + (r.id == 0 ? 1 : 0));
            fprintf(log, " returned %lu bytes:", n);
            for (size_t i = 0; i < length; ++i) fprintf(log, " %02x", bytes[i]);
            fputc('\n', log);
        } else fprintf(log, " Windows error %lu\n", error);
    }
    CloseHandle(h);
}

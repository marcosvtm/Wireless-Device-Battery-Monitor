#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>

namespace {
int opens, reads, linkReply = 1, batteryReply = 30;
HidColl Dongle() {
    HidColl c;
    c.vid = 0x0ecb; c.pid = 0x206b; c.iface = 3;
    c.caps.UsagePage = 1; c.caps.Usage = 0;
    c.caps.InputReportByteLength = 2; c.caps.FeatureReportByteLength = 64;
    return c;
}
}
std::wstring Hex4(USHORT v) { assert(v == 0x206b); return L"206B"; }
bool HidDev::OpenReadOnly(const HidColl&) { ++opens; return true; }
bool HidDev::GetFeatureByte(BYTE id, BYTE& b) {
    assert(id == 0x68 || id == 0x49);
    ++reads;
    int value = id == 0x68 ? linkReply : batteryReply;
    if (value < 0) return false;
    b = static_cast<BYTE>(value);
    return true;
}
// A Quantum 350 must never enter the HIDV3 write path.
bool HidDev::Open(const HidColl&) { assert(false); return false; }
bool HidDev::SetFeature(const std::vector<BYTE>&) { assert(false); return false; }
void HidDev::Flush() { assert(false); }
int HidDev::Read(std::vector<BYTE>&, int) { assert(false); return -1; }
void HidDev::Close() {}
int main() {
    auto c = Dongle();
    auto media = c, keyboard = c, wrongPid = c, wrongIface = c, shortReport = c, wrongVendor = c;
    media.caps.UsagePage = 0xc; media.caps.Usage = 1; media.caps.FeatureReportByteLength = 0;
    keyboard.caps.Usage = 6; wrongPid.pid++; wrongIface.iface = 2; shortReport.caps.FeatureReportByteLength = 1;
    wrongVendor.vid = 0x1234;
    PollContext ctx;
    ctx.colls = {media, keyboard, wrongPid, wrongIface, shortReport, wrongVendor};
    PollJbl(ctx);
    assert(opens == 0 && ctx.out.empty());
    for (auto level : {0, 30, 100, 101, 255, -1}) {
        ctx = {};
        ctx.colls = {media, c};
        batteryReply = level;
        PollJbl(ctx);
        if (level < 0 || level > 100) { assert(ctx.out.empty()); continue; }
        assert(ctx.out.size() == 1 && ctx.out.front().percent == level && ctx.out.front().kind == DeviceKind::Headset);
        assert(ctx.out.front().key == L"jbl:206B" && !ctx.out.front().charging);
        int before = opens;
        PollJbl(ctx);
        assert(opens == before);
    }
    for (int link : {0, -1, 255}) {
        ctx = {};
        ctx.colls = {c};
        reads = 0;
        linkReply = link;
        PollJbl(ctx);
        assert(ctx.out.empty() && reads == 1);
    }
    puts("Quantum 350 HIDV1 polling tests passed");
}

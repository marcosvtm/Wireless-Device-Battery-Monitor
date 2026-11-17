#pragma once
#include "hid.h"
#include <cstdio>

// Requests are selected from Windows' parsed descriptor for this specific receiver.
struct Request {
    BYTE id;
    HIDP_REPORT_TYPE type;
};
struct Probe {
    HidColl collection;
    std::vector<Request> requests;
};
Probe Describe(FILE* log, const HidColl& c);
void Sample(FILE* log, const Probe& probe, ULONGLONG start);

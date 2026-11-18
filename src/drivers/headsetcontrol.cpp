// In-process bridge to HeadsetControl (github.com/Sapd/HeadsetControl), which supports many more
// headsets than the built-in drivers. The HeadsetControl static library (third_party/HeadsetControl)
// is linked into the exe and queried through its C API; headsets not already handled here are added.
#include "drivers.h"

#include <headsetcontrol_c.h>

#include <string>

namespace {

std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// One headset as last reported by the HeadsetControl library.
struct HscDevice { USHORT vid, pid; DeviceStatus status; };

std::vector<HscDevice> Discover() {
    std::vector<HscDevice> out;
    hsc_headset_t* headsets = nullptr;
    int count = hsc_discover(&headsets);
    for (int i = 0; i < count; i++) {
        if (!hsc_supports(headsets[i], HSC_CAP_BATTERY_STATUS)) continue;
        HscDevice h;
        h.vid = (USHORT)hsc_get_vendor_id(headsets[i]);
        h.pid = (USHORT)hsc_get_product_id(headsets[i]);
        DeviceStatus& s = h.status;
        s.key = L"hsc:" + Hex4(h.vid) + L":" + Hex4(h.pid);
        const char* name = hsc_get_name(headsets[i]);
        s.name = name && *name ? Widen(name) : L"Headset";
        s.kind = DeviceKind::Headset;
        s.via = L"HeadsetControl";
        hsc_battery_t bat;
        s.connected = false;  // query failed or battery unavailable: hide the device, like the old JSON path
        if (hsc_get_battery(headsets[i], &bat) == HSC_RESULT_OK) {
            s.charging = bat.status == HSC_BATTERY_CHARGING;
            s.connected = bat.status == HSC_BATTERY_AVAILABLE || bat.status == HSC_BATTERY_CHARGING;
            if (s.connected && bat.level_percent >= 0) s.percent = Clamp100(bat.level_percent);
        }
        out.push_back(h);
    }
    if (count > 0) hsc_free_headsets(headsets, count);
    return out;
}

// Discovery can take a while per headset (a short HID timeout keeps a sleeping headset from stalling
// it), so it runs on its own thread and the regular poll uses its latest result instead of waiting.
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<HscDevice> g_devices;
ULONGLONG g_updated = 0;
LONG g_running = 0;

void Refresh() {
    std::vector<HscDevice> found = Discover();
    AcquireSRWLockExclusive(&g_lock);
    g_devices = found;
    g_updated = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
}

DWORD WINAPI RefreshThread(LPVOID) {
    Refresh();
    InterlockedExchange(&g_running, 0);
    return 0;
}

}  // namespace

void PollHeadsetControl(PollContext& ctx) {
    // Library-wide settings are written once, before the first (synchronous) discovery, so no
    // background discovery thread ever races on them.
    static const bool hscInit = [] {
        hsc_set_device_timeout(1500);  // same short timeout the CLI bridge used
        wchar_t test[8];
        if (GetEnvironmentVariableW(L"WB_HSC_TEST_DEVICE", test, 8)) hsc_enable_test_device(true);
        return true;
    }();
    (void)hscInit;
    ULONGLONG now = GetTickCount64();
    if (g_updated == 0) {
        Refresh();  // first poll: wait for it, so the first display (and --list) is complete
    } else if (now - g_updated > 50000 && InterlockedCompareExchange(&g_running, 1, 0) == 0) {
        HANDLE t = CreateThread(nullptr, 0, RefreshThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
        else InterlockedExchange(&g_running, 0);
    }
    AcquireSRWLockShared(&g_lock);
    std::vector<HscDevice> devices = g_devices;
    ReleaseSRWLockShared(&g_lock);
    for (const HscDevice& h : devices)
        if (!ctx.IsClaimed(h.vid, h.pid)) ctx.Add(h.status, h.vid, h.pid);
}

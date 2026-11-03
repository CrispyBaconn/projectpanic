// Switch-specific functions (libnx), in a separate file so <switch.h> isn't mixed with the rest.
// Outside the Switch the file is empty (Makefile.wiiu builds all the .cpp files in platform/).
#ifdef __SWITCH__
#include <switch.h>
#include <ctime>
#include <cstring>

// system local time (newlib on Switch returns UTC: only the time service knows the time zone)
extern "C" bool s23_switch_localtime(struct tm* out) {
    u64 ts = 0; TimeCalendarTime ct; TimeCalendarAdditionalInfo ai;
    if (R_FAILED(timeGetCurrentTime(TimeType_LocalSystemClock, &ts))) return false;
    if (R_FAILED(timeToCalendarTimeWithMyRule(ts, &ct, &ai))) return false;
    memset(out, 0, sizeof *out);
    out->tm_year = ct.year - 1900; out->tm_mon = ct.month - 1; out->tm_mday = ct.day;
    out->tm_hour = ct.hour; out->tm_min = ct.minute; out->tm_sec = ct.second;
    out->tm_wday = (int)ai.wday; out->tm_yday = (int)ai.yday;
    return true;
}
#endif

#ifdef __SWITCH__
// thread on a specific core (0..2). libnx's pthread would put them all on the process's core.
static Thread g_thr[8]; static int g_nthr = 0;
static void thr_entry(void* a) { ((void* (*)(void*))a)(nullptr); }
extern "C" bool s23_switch_thread(void* (*fn)(void*), size_t stack, int core, int prio) {
    if (g_nthr >= 8) return false;
    stack = (stack + 0xfff) & ~(size_t)0xfff;                              // multiple of 4 KB
    Thread* t = &g_thr[g_nthr];
    if (R_FAILED(threadCreate(t, thr_entry, (void*)fn, nullptr, stack, prio, core))) return false;
    if (R_FAILED(threadStart(t))) { threadClose(t); return false; }
    g_nthr++; return true;
}
extern "C" void s23_switch_thread_exit() { threadExit(); }
extern "C" void s23_switch_join_all() {
    for (int i = 0; i < g_nthr; i++) { threadWaitForExit(&g_thr[i]); threadClose(&g_thr[i]); }
    g_nthr = 0;
}
#endif

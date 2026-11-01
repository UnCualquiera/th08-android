#include "modern/windows_runtime.hpp"

#if !defined(__ANDROID__)
#include <execinfo.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <windows.h>
#if defined(__ANDROID__)
#include <SDL.h>
#include <atomic>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unwind.h>
#endif

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPTSTR, int);

namespace th08
{
struct AnmVm;
struct Effect;

int __fastcall EffectRandomSplashInit(Effect *);
int __fastcall EffectRandomSplashUpdate(Effect *);
int __fastcall EffectRandomSplashBigInit(Effect *);
int __fastcall EffectOrbitInit(Effect *);
int __fastcall EffectOrbitUpdate(Effect *);

int __fastcall UpdateExpandingWavyRadialTrail(Effect *);
int __fastcall UpdateExpandingPositiveDiagonalRadialTrail(Effect *);
int __fastcall UpdateExpandingNegativeDiagonalRadialTrail(Effect *);
int __fastcall UpdateExpandingOctagonalRadialTrail(Effect *);
int __fastcall UpdateExpandingTwelveSegmentRadialTrail(Effect *);
int __fastcall UpdateBarrierRadialEffect(Effect *);
int __fastcall InitializeBarrierRadialEffect(Effect *);
int __fastcall InitializeRotatingBarrierRadialEffect(Effect *);
int __fastcall UpdateExpandingOrthogonalRadialTrail(Effect *);

int __fastcall InitializeTintedBossTrackingCameraParticle(Effect *);
int __fastcall UpdateTintedBossTrackingCameraParticle(Effect *);
int __fastcall InitializeRisingBossTrackingCameraParticle(Effect *);
int __fastcall UpdateRisingBossTrackingCameraParticle(Effect *);
int __fastcall InitializeRandomDirectionalOffset(Effect *);
int __fastcall UpdateDirectionalOffset60(Effect *);
int __fastcall TrackPlayerUntilAnimationEnds(Effect *);
int __fastcall UpdateDirectionalOffset240(Effect *);
int __fastcall UpdateSpinningCameraParticle(Effect *);
int __fastcall InitializeSpinningCameraParticle(Effect *);
int __fastcall InitializeDirectionalOffset(Effect *);
int __fastcall UpdateEasedDirectionalOffset(Effect *);
int __fastcall KeepTrailAlive(Effect *);
int __fastcall InitializeTrailOffset(Effect *);
int __fastcall InitializeRadialTrail(Effect *);
int __fastcall InitializeAlternateLayerRadialTrail(Effect *);
int __fastcall SyncRadialTrailRadius(Effect *);
int __fastcall SyncRadialTrailShape(Effect *);
int __fastcall UpdateTimedRadialTrail(Effect *);
int __fastcall UpdateFadingRadialTrail(Effect *);
int __fastcall SyncAnchoredRadialTrail(Effect *);

// This retail table entry points at an AnmVm member. On the 32-bit Linux ABI
// its code entry receives `this` as the first stack argument, matching the
// reconstructed effect callback invocation.
extern "C" int UpdatePulsingRadialTrailCallback(AnmVm *) asm("_ZN4th085AnmVm24UpdatePulsingRadialTrailEv");

namespace modern
{
struct ModernEffectTemplate
{
    int32_t scriptIdx;
    uintptr_t update;
    uintptr_t initialize;
};

// These aliases bind the semantic target globals exported at fixed addresses
// by th08-layout.ld.  Keep the compatibility storage types local to the Linux
// runtime: the reconstructed VC7 declarations retain their original owners.
extern int32_t g_ModernLastSpellCountStorage asm("_ZN4th0816g_LastSpellCountE");
extern ModernEffectTemplate g_ModernEffectTemplatesStorage[66]
    asm("_ZN4th0817g_EffectTemplatesE");
extern int32_t g_ModernGuiStageClearBonusesStorage[9]
    asm("_ZN4th0822g_GuiStageClearBonusesE");
extern uint32_t g_ModernGuiMessageTextColorsStorage[12][4]
    asm("_ZN4th0822g_GuiMessageTextColorsE");

namespace
{
int g_argumentCount;
char **g_arguments;
volatile sig_atomic_t g_reportingCrash;

void WriteCrashLine(int file, const char *line)
{
    if (line != NULL) write(file, line, strlen(line));
}

void ReportFatalSignal(int signalNumber, siginfo_t *signalInfo, void *)
{
    if (g_reportingCrash)
        _exit(128 + signalNumber);
    g_reportingCrash = 1;

    int file = open("modern-crash.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file >= 0)
    {
        char line[160];
        snprintf(line, sizeof(line), "signal=%d fault-address=%p pid=%ld\n", signalNumber,
                 signalInfo != NULL ? signalInfo->si_addr : NULL, static_cast<long>(getpid()));
        WriteCrashLine(file, line);
#if !defined(__ANDROID__)
        void *frames[64];
        int frameCount = backtrace(frames, sizeof(frames) / sizeof(frames[0]));
        backtrace_symbols_fd(frames, frameCount, file);
#endif
        fsync(file);
        close(file);
    }

    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
    _exit(128 + signalNumber);
}

void InstallSignalHandler(int signalNumber)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = ReportFatalSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(signalNumber, &action, NULL);
}

uintptr_t CodeAddress(int (__fastcall *callback)(AnmVm *))
{
    return reinterpret_cast<uintptr_t>(callback);
}

uintptr_t CodeAddress(int (__fastcall *callback)(Effect *))
{
    return reinterpret_cast<uintptr_t>(callback);
}

void InitializeTargetData()
{
    static const ModernEffectTemplate effectTemplates[66] = {
        {28, 0, 0}, {29, 0, 0}, {30, 0, 0},
        {31, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashBigInit)},
        {36, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashInit)},
        {37, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashInit)},
        {38, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashInit)},
        {39, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashInit)},
        {40, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashInit)},
        {41, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashInit)},
        {42, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashInit)},
        {43, CodeAddress(EffectRandomSplashUpdate), CodeAddress(EffectRandomSplashInit)},
        {44, 0, 0},
        {45, CodeAddress(EffectOrbitUpdate), CodeAddress(EffectOrbitInit)},
        {45, CodeAddress(EffectOrbitUpdate), CodeAddress(EffectOrbitInit)},
        {45, CodeAddress(EffectOrbitUpdate), CodeAddress(EffectOrbitInit)},
        {0, 0, 0},
        {32, CodeAddress(UpdateDirectionalOffset60), CodeAddress(InitializeRandomDirectionalOffset)},
        {33, CodeAddress(UpdateDirectionalOffset240), CodeAddress(InitializeRandomDirectionalOffset)},
        {51, CodeAddress(UpdateSpinningCameraParticle), CodeAddress(InitializeSpinningCameraParticle)},
        {56, 0, 0},
        {52, CodeAddress(UpdateEasedDirectionalOffset), CodeAddress(InitializeDirectionalOffset)},
        {54, CodeAddress(TrackPlayerUntilAnimationEnds), 0},
        {104, CodeAddress(KeepTrailAlive), 0},
        {104, CodeAddress(KeepTrailAlive), 0},
        {35, 0, 0},
        {53, CodeAddress(UpdateEasedDirectionalOffset), CodeAddress(InitializeDirectionalOffset)},
        {34, CodeAddress(UpdateDirectionalOffset60), CodeAddress(InitializeRandomDirectionalOffset)},
        {57, 0, 0}, {58, 0, 0}, {59, 0, 0}, {60, 0, 0},
        {48, 0, 0}, {49, 0, 0}, {50, 0, 0},
        {88, CodeAddress(SyncRadialTrailRadius), CodeAddress(InitializeRadialTrail)},
        {88, CodeAddress(UpdateBarrierRadialEffect), CodeAddress(InitializeBarrierRadialEffect)},
        {92, CodeAddress(UpdateBarrierRadialEffect), CodeAddress(InitializeRotatingBarrierRadialEffect)},
        {71, 0, 0},
        {76, CodeAddress(SyncRadialTrailRadius), CodeAddress(InitializeRadialTrail)},
        {81, CodeAddress(SyncRadialTrailShape), CodeAddress(InitializeRadialTrail)},
        {82, CodeAddress(UpdatePulsingRadialTrailCallback), CodeAddress(InitializeRadialTrail)},
        {83, CodeAddress(UpdateExpandingWavyRadialTrail), CodeAddress(InitializeRadialTrail)},
        {83, CodeAddress(UpdateExpandingPositiveDiagonalRadialTrail), CodeAddress(InitializeRadialTrail)},
        {83, CodeAddress(UpdateExpandingNegativeDiagonalRadialTrail), CodeAddress(InitializeRadialTrail)},
        {83, CodeAddress(UpdateExpandingOctagonalRadialTrail), CodeAddress(InitializeRadialTrail)},
        {84, CodeAddress(UpdateExpandingTwelveSegmentRadialTrail), CodeAddress(InitializeRadialTrail)},
        {72, 0, 0},
        {85, CodeAddress(UpdateExpandingOrthogonalRadialTrail), CodeAddress(InitializeRadialTrail)},
        {86, CodeAddress(SyncRadialTrailRadius), CodeAddress(InitializeRadialTrail)},
        {80, CodeAddress(UpdateTimedRadialTrail), CodeAddress(InitializeRadialTrail)},
        {73, CodeAddress(UpdateTintedBossTrackingCameraParticle),
         CodeAddress(InitializeTintedBossTrackingCameraParticle)},
        {77, CodeAddress(SyncRadialTrailRadius), CodeAddress(InitializeRadialTrail)},
        {88, CodeAddress(UpdateFadingRadialTrail), CodeAddress(InitializeRadialTrail)},
        {88, CodeAddress(UpdateFadingRadialTrail), CodeAddress(InitializeRadialTrail)},
        {87, CodeAddress(SyncRadialTrailShape), CodeAddress(InitializeRadialTrail)},
        {96, CodeAddress(SyncRadialTrailShape), CodeAddress(InitializeAlternateLayerRadialTrail)},
        {55, 0, 0},
        {100, CodeAddress(SyncRadialTrailShape), CodeAddress(InitializeAlternateLayerRadialTrail)},
        {78, CodeAddress(SyncRadialTrailRadius), CodeAddress(InitializeRadialTrail)},
        {102, 0, CodeAddress(InitializeTrailOffset)},
        {103, 0, CodeAddress(InitializeTrailOffset)},
        {75, 0, 0},
        {74, CodeAddress(UpdateRisingBossTrackingCameraParticle),
         CodeAddress(InitializeRisingBossTrackingCameraParticle)},
        {77, CodeAddress(SyncAnchoredRadialTrail), CodeAddress(InitializeRadialTrail)},
        {98, CodeAddress(SyncRadialTrailShape), CodeAddress(InitializeAlternateLayerRadialTrail)},
    };
    static const int32_t stageScoreTables[9] = {
        1000000, 1500000, 2000000, 2500000, 2500000, 3000000, 4000000, 6000000, 6660000,
    };
    static const uint32_t messageTextColors[12][4] = {
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
        {0x00e8f0ff, 0x00f0e8ff, 0x00ffe8f0, 0x00ffe8f0},
    };
    g_ModernLastSpellCountStorage = 43;
    memcpy(g_ModernEffectTemplatesStorage, effectTemplates, sizeof(effectTemplates));
    memcpy(g_ModernGuiStageClearBonusesStorage, stageScoreTables, sizeof(stageScoreTables));
    memcpy(g_ModernGuiMessageTextColorsStorage, messageTextColors, sizeof(messageTextColors));
}
}

namespace
{
// Creates and removes a file in the current directory, which is exactly the
// capability the game needs there for th08.cfg, its saves and its replays.
bool DirectoryIsWritable()
{
    FILE *probe = fopen(".th08_write_probe", "wb");
    if (probe == NULL) return false;
    fclose(probe);
    return remove(".th08_write_probe") == 0;
}

// Monotonic seconds since an unspecified epoch, used to timestamp the
// diagnostic logs so an export can tell how far into a session each file was
// opened and how long the game stayed alive.
long long MonotonicSeconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec);
}

// The diagnostic export can only be taken after the app restarts, so when a
// previous session died without a clean shutdown (its heartbeat still says
// "running"), the heartbeat is moved aside here instead of being overwritten
// by the new session.
void PreservePreviousHeartbeat()
{
    FILE *file = fopen("modern-heartbeat.txt", "r");
    if (file == NULL) return;
    char line[256];
    bool running = false;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (strstr(line, "status=running") != NULL) { running = true; break; }
    }
    fclose(file);
    if (!running) return;
    remove("modern-heartbeat-prev.txt");
    rename("modern-heartbeat.txt", "modern-heartbeat-prev.txt");
}
} // namespace

#if defined(__ANDROID__)
namespace
{
// A device without simpleperf, debuggerd or root cannot be profiled from the
// outside, so a frozen game loop diagnoses itself: the present path rewrites
// modern-heartbeat.txt once a second, and this watchdog notices when that
// file has stopped changing for three seconds while the session is still
// marked "running". It then signals every thread with SIGUSR2 (the game
// threads keep SIGUSR1, SIGQUIT and SIGPIPE blocked), and each handler
// records its exact instruction pointer (taken from the signal context)
// plus its unwind trace into modern-stall.txt. The raw addresses are
// symbolized offline against the unstripped build objects.
std::atomic<int> g_StallDumped{0};

struct UnwindBuffer
{
    char *buffer;
    size_t offset;
    size_t capacity;
};

_Unwind_Reason_Code AppendUnwindFrame(struct _Unwind_Context *context, void *argument)
{
    UnwindBuffer *unwind = static_cast<UnwindBuffer *>(argument);
    if (unwind->offset + 32 < unwind->capacity)
    {
        unwind->offset += snprintf(unwind->buffer + unwind->offset, unwind->capacity - unwind->offset,
                                   "  unwind=0x%08lx\n", (unsigned long)_Unwind_GetIP(context));
    }
    return _URC_NO_REASON;
}

void StallHandler(int, siginfo_t *, void *context)
{
    const ucontext_t *signalContext = static_cast<const ucontext_t *>(context);
    char buffer[8192];
    size_t offset = snprintf(buffer, sizeof(buffer), "tid=%ld\n", (long)syscall(SYS_gettid));
#if defined(__aarch64__)
    const unsigned long pc = signalContext->uc_mcontext.pc;
    const unsigned long long framePointer =
        signalContext->uc_mcontext.regs[29];
    const unsigned long long linkRegister = signalContext->uc_mcontext.regs[30];
    const unsigned long long stackPointer = signalContext->uc_mcontext.sp;
#elif defined(__arm__)
    const unsigned long pc = signalContext->uc_mcontext.arm_pc;
    const unsigned long long framePointer = signalContext->uc_mcontext.arm_fp;
    const unsigned long long linkRegister = signalContext->uc_mcontext.arm_lr;
    const unsigned long long stackPointer = signalContext->uc_mcontext.arm_sp;
#else
    const unsigned long pc = 0;
    const unsigned long long framePointer = 0;
    const unsigned long long linkRegister = 0;
    const unsigned long long stackPointer = 0;
#endif
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "  pc=0x%08lx\n", pc);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "  lr=0x%08lx\n",
                       (unsigned long)linkRegister);
    // The handler runs on the interrupted thread, so its stack can be read
    // directly: walk the AAPCS64/ARM frame-pointer chain by hand, without
    // any unwinding library (the signal trampoline confuses _Unwind).
    unsigned long long frame = framePointer;
    for (int level = 0; level < 48 && frame != 0; level++)
    {
        unsigned long long savedFrame = 0;
        unsigned long long returnAddress = 0;
        if (frame + 16 > stackPointer + 1048576) break;
        __builtin_memcpy(&savedFrame, reinterpret_cast<const void *>(frame), sizeof(savedFrame));
        __builtin_memcpy(&returnAddress, reinterpret_cast<const void *>(frame + (sizeof(unsigned long long) == 8 ? 8 : 4)),
                         sizeof(returnAddress));
        if (returnAddress == 0 || savedFrame < frame) break;
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "  frame pc=0x%08lx\n",
                           (unsigned long)returnAddress);
        frame = savedFrame;
    }
    const int file = open("modern-stall.txt", O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (file >= 0)
    {
        write(file, buffer, offset);
        close(file);
    }
    // The full memory map identifies the region the interrupted code lives in
    // (and where the giant allocation landed). Only the main thread dumps it.
    if (syscall(SYS_gettid) == syscall(SYS_getpid))
    {
        FILE *maps = fopen("/proc/self/maps", "r");
        if (maps != NULL)
        {
            FILE *out = fopen("modern-stall.txt", "a");
            if (out != NULL)
            {
                char line[512];
                while (fgets(line, sizeof(line), maps) != NULL)
                {
                    fputs(line, out);
                }
                fclose(out);
            }
            fclose(maps);
        }
    }
}

void AppendThreadDump(long tid)
{
    if (tid == syscall(SYS_gettid)) return;
    FILE *out = fopen("modern-stall.txt", "a");
    if (out != NULL)
    {
        fprintf(out, "tid=%ld signaling\n", (long)tid);
        fclose(out);
    }
    if (syscall(SYS_tgkill, syscall(SYS_getpid), tid, SIGUSR2) != 0)
    {
        out = fopen("modern-stall.txt", "a");
        if (out != NULL)
        {
            fprintf(out, "tid=%ld signal failed\n", (long)tid);
            fclose(out);
        }
        return;
    }
    // Give the handler time to write its own section before moving on.
    usleep(20000);
}

void DumpStalledThreads()
{
    long long frames = -1;
    FILE *heartbeat = fopen("modern-heartbeat.txt", "r");
    if (heartbeat != NULL)
    {
        char line[256];
        while (fgets(line, sizeof(line), heartbeat) != NULL)
        {
            const char *found = strstr(line, "frames_total=");
            if (found != NULL) frames = atoll(found + strlen("frames_total="));
        }
        fclose(heartbeat);
    }
    FILE *out = fopen("modern-stall.txt", "a");
    if (out != NULL)
    {
        fprintf(out, "=== stall detected at epoch=%lld frames_total=%lld ===\n",
                static_cast<long long>(time(NULL)), frames);
        fclose(out);
    }
    DIR *tasks = opendir("/proc/self/task");
    if (tasks == NULL)
    {
        out = fopen("modern-stall.txt", "a");
        if (out != NULL)
        {
            fprintf(out, "opendir /proc/self/task failed\n");
            fclose(out);
        }
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(tasks)) != NULL)
    {
        if (entry->d_name[0] == '.') continue;
        const long tid = atol(entry->d_name);
        if (tid > 0) AppendThreadDump(tid);
    }
    closedir(tasks);
    out = fopen("modern-stall.txt", "a");
    if (out != NULL)
    {
        fprintf(out, "=== dump complete ===\n");
        fclose(out);
    }
}

void *StallWatchdog(void *)
{
    struct stat previous;
    memset(&previous, 0, sizeof(previous));
    bool havePrevious = false;
    int frozenSeconds = 0;
    for (;;)
    {
        sleep(1);
        struct stat info;
        if (stat("modern-heartbeat.txt", &info) != 0)
        {
            havePrevious = false;
            frozenSeconds = 0;
            continue;
        }
        FILE *heartbeat = fopen("modern-heartbeat.txt", "r");
        if (heartbeat != NULL)
        {
            char line[256];
            bool closed = false;
            while (fgets(line, sizeof(line), heartbeat) != NULL)
            {
                if (strstr(line, "status=closed") != NULL) { closed = true; break; }
            }
            fclose(heartbeat);
            if (closed)
            {
                // The game is ending by itself; a heartbeat that stops moving
                // here is a shutdown, not a stall.
                havePrevious = false;
                frozenSeconds = 0;
                continue;
            }
        }
        if (havePrevious && info.st_mtim.tv_sec == previous.st_mtim.tv_sec &&
            info.st_mtim.tv_nsec == previous.st_mtim.tv_nsec)
        {
            if (++frozenSeconds == 3 && g_StallDumped.exchange(1) == 0)
            {
                DumpStalledThreads();
            }
        }
        else
        {
            frozenSeconds = 0;
            previous = info;
            havePrevious = true;
        }
    }
    return NULL;
}

void StartStallWatchdog()
{
    // The game threads keep SIGUSR1, SIGQUIT and SIGPIPE blocked (an
    // SDL/Java thread inheritance), so the watchdog communicates over
    // SIGUSR2, which no game thread masks.
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = StallHandler;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR2, &action, NULL);

    FILE *out = fopen("modern-stall.txt", "a");
    if (out != NULL)
    {
        fprintf(out, "=== session start epoch=%lld ===\n", static_cast<long long>(time(NULL)));
        fclose(out);
    }

    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_create(&thread, &attributes, StallWatchdog, NULL);
    pthread_attr_destroy(&attributes);
}
} // namespace
#endif

void WriteAndroidBootStage(const char *status, const char *stage)
{
#if defined(__ANDROID__)
    FILE *boot = fopen("modern-boot.txt", "w");
    if (boot != NULL)
    {
        fprintf(boot, "status=%s stage=%s epoch=%lld\n",
                status != NULL ? status : "unknown", stage != NULL ? stage : "unknown",
                static_cast<long long>(time(NULL)));
        fclose(boot);
    }
#else
    (void)status;
    (void)stage;
#endif
}

bool ConfigureDataDirectory()
{
    const char *directory = NULL;
    for (int index = 1; index < g_argumentCount; ++index)
    {
        if (strcmp(g_arguments[index], "--data-dir") == 0)
        {
            if (++index >= g_argumentCount)
            {
                fprintf(stderr, "th08-modern: --data-dir requires a directory path\n");
                return false;
            }
            directory = g_arguments[index];
        }
        else if (strncmp(g_arguments[index], "--data-dir=", 11) == 0)
        {
            directory = g_arguments[index] + 11;
        }
    }

#if defined(__ANDROID__)
    // The setup activity stores the selected game folder in internal storage
    // before the game activity starts, so a cold launch finds it here. The
    // stored path always names a real filesystem directory: either a directly
    // readable folder on the primary storage or the app-private copy.
    static char androidDirectory[1024];
    if (directory == NULL)
    {
        const char *internal = SDL_AndroidGetInternalStoragePath();
        if (internal != NULL)
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/th08_data_dir.txt", internal);
            FILE *file = fopen(path, "r");
            if (file != NULL)
            {
                if (fgets(androidDirectory, sizeof(androidDirectory), file) != NULL)
                {
                    size_t length = strlen(androidDirectory);
                    while (length > 0 && (androidDirectory[length - 1] == '\n' ||
                                          androidDirectory[length - 1] == '\r'))
                        androidDirectory[--length] = '\0';
                    if (length > 0) directory = androidDirectory;
                }
                fclose(file);
            }
        }
    }
#endif

    if (directory != NULL && (directory[0] == '\0' || chdir(directory) != 0))
    {
#if defined(__ANDROID__)
        FILE *boot = fopen("modern-boot.txt", "w");
        if (boot != NULL)
        {
            fprintf(boot, "status=failed stage=unable-to-enter-data-directory epoch=%lld\n",
                    static_cast<long long>(time(NULL)));
            fclose(boot);
        }
#endif
        fprintf(stderr, "th08-modern: unable to enter data directory: %s\n", directory);
        return false;
    }

    struct stat info;
    if (stat("th08.dat", &info) != 0 || !S_ISREG(info.st_mode))
    {
#if defined(__ANDROID__)
        FILE *boot = fopen("modern-boot.txt", "w");
        if (boot != NULL)
        {
            fprintf(boot, "status=failed stage=missing-th08-dat epoch=%lld\n",
                    static_cast<long long>(time(NULL)));
            fclose(boot);
        }
#endif
        fprintf(stderr, "th08-modern: selected directory does not contain th08.dat\n");
        return false;
    }

    // The game writes th08.cfg, its saves and its replays into this working
    // directory. On Android a folder can be entered and read while plain-path
    // writes to it are rejected, and that would only surface later as a fatal
    // config error, so writability is verified here, right after the data
    // check, with a fallback to the app-private copy the setup activity keeps
    // populated.
    if (!DirectoryIsWritable())
    {
#if defined(__ANDROID__)
        const char *internal = SDL_AndroidGetInternalStoragePath();
        char fallback[1024];
        if (internal != NULL &&
            snprintf(fallback, sizeof(fallback), "%s/game", internal) > 0 &&
            chdir(fallback) == 0 && stat("th08.dat", &info) == 0 &&
            S_ISREG(info.st_mode) && DirectoryIsWritable())
        {
            fprintf(stderr,
                    "th08-modern: data directory rejects writes, using the app-private copy: %s\n",
                    fallback);
        }
        else
#endif
        {
#if defined(__ANDROID__)
            FILE *boot = fopen("modern-boot.txt", "w");
            if (boot != NULL)
            {
                fprintf(boot, "status=failed stage=data-directory-not-writable epoch=%lld\n",
                        static_cast<long long>(time(NULL)));
                fclose(boot);
            }
#endif
            fprintf(stderr, "th08-modern: data directory is not writable: %s\n", strerror(errno));
            return false;
        }
    }
    {
        char workingDirectory[1024];
        if (getcwd(workingDirectory, sizeof(workingDirectory)) != NULL)
            fprintf(stderr, "th08-modern: game data directory: %s\n", workingDirectory);
    }
    PreservePreviousHeartbeat();
#if defined(__ANDROID__)
    FILE *boot = fopen("modern-boot.txt", "w");
    if (boot != NULL)
    {
        fprintf(boot, "status=starting stage=data-directory epoch=%lld\n",
                static_cast<long long>(time(NULL)));
        fclose(boot);
    }
#endif
    unlink("modern-files.txt");
    unlink("modern-crash.txt");
    unlink("modern-render.txt");
    unlink("modern-enemy-render.csv");
#if defined(__ANDROID__)
    StartStallWatchdog();
#endif
    return true;
}

void InstallCrashReporter()
{
    InitializeTargetData();
    if (getenv("TH08_DISABLE_CRASH_REPORTER") != NULL)
        return;
    InstallSignalHandler(SIGSEGV);
    InstallSignalHandler(SIGABRT);
    InstallSignalHandler(SIGFPE);
    InstallSignalHandler(SIGILL);
    InstallSignalHandler(SIGBUS);
}

void LogArchiveRequest(const char *path)
{
    FILE *file = fopen("modern-files.txt", "ab");
    if (file == NULL)
        return;
    fprintf(file, "t=%llds thread=%08lx path=%s\n", MonotonicSeconds(), (unsigned long)GetCurrentThreadId(),
            path != NULL ? path : "<null>");
    fclose(file);
}

void SetArguments(int argc, char **argv)
{
    g_argumentCount = argc;
    g_arguments = argv;
}

// Rewrites the heartbeat on the way out: a file that still says
// "status=running" at the next launch is a session that never exited cleanly,
// while "closed" means the game ended by itself.
void MarkCleanExit()
{
#if defined(__ANDROID__)
    FILE *heartbeat = fopen("modern-heartbeat.txt", "w");
    if (heartbeat == NULL) return;
    fprintf(heartbeat, "status=closed epoch=%lld\n", static_cast<long long>(time(NULL)));
    fclose(heartbeat);
#endif
}
} // namespace modern
} // namespace th08

int main(int argc, char **argv)
{
    th08::modern::SetArguments(argc, argv);
    int result = WinMain(NULL, NULL, NULL, 0);
    th08::modern::MarkCleanExit();
    return result;
}

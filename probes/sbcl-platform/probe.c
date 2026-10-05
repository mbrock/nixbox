/* Do not let the App SDK's inline VirtualAlloc/Protect wrappers conceal which
 * API is tested: SBCL uses the real desktop exports, not the FromApp exports. */
#define VirtualAlloc sdk_app_VirtualAlloc
#define VirtualProtect sdk_app_VirtualProtect
#include <windows.h>
#undef VirtualAlloc
#undef VirtualProtect
#include <process.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The App SDK deliberately omits these declarations. Test the actual imports,
 * not a globally reopened desktop partition or an assumed UWP guarantee. */
__declspec(dllimport) PVOID WINAPI AddVectoredExceptionHandler(ULONG, PVECTORED_EXCEPTION_HANDLER);
__declspec(dllimport) ULONG WINAPI RemoveVectoredExceptionHandler(PVOID);
__declspec(dllimport) LPVOID WINAPI VirtualAlloc(LPVOID, SIZE_T, DWORD, DWORD);
__declspec(dllimport) BOOL WINAPI VirtualProtect(LPVOID, SIZE_T, DWORD, PDWORD);

static FILE *report;
static int failures;
#define LOG(...) do { fprintf(report, __VA_ARGS__); fputc('\n', report); fflush(report); } while (0)
#define CHECK(name, ok) do { int passed = !!(ok); LOG("%s: %s", passed ? "PASS" : "FAIL", name); failures += !passed; } while (0)

typedef int (*code_function)(int);

/* Keep SEH in C functions without C++ object destruction. Capture exceptions
 * so an NX page or a rejected code path produces a report rather than a crash. */
static int call_code(void *code, int argument, int *value, DWORD *exception)
{
    __try {
        *value = ((code_function)code)(argument);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *exception = GetExceptionCode();
        return 0;
    }
}

static void generated_code(void)
{
    /* lea eax,[rcx+7]; ret -- Win64's first integer argument is ECX.
     * Asymmetric inputs distinguish executing code from returning a constant. */
    const unsigned char instructions[] = { 0x8d, 0x41, 0x07, 0xc3 };
    unsigned char *code = VirtualAllocFromApp(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ULONG old;
    int value = 0;
    DWORD exception = 0;
    CHECK("allocate writable code page", code != NULL);
    if (!code) { LOG("  GetLastError=%lu", GetLastError()); return; }
    memcpy(code, instructions, sizeof instructions);
    SetLastError(0);
    int executable = VirtualProtectFromApp(code, 4096, PAGE_EXECUTE_READ, &old);
    DWORD error = executable ? 0 : GetLastError();
    LOG("RW -> RX: %s (error=%lu)", executable ? "accepted" : "rejected", error);
    CHECK("generated code becomes executable", executable);
    if (executable) {
        CHECK("flush generated instructions", FlushInstructionCache(GetCurrentProcess(), code, sizeof instructions));
        LOG("Executing generated x64 code...");
        CHECK("f(35)=42", call_code(code, 35, &value, &exception) && value == 42);
        CHECK("f(-19)=-12", call_code(code, -19, &value, &exception) && value == -12);
        if (exception) LOG("  execution exception=0x%08lx", exception);
        int writable = VirtualProtectFromApp(code, 4096, PAGE_READWRITE, &old);
        CHECK("RX -> RW for patching", writable);
        if (writable) {
            code[2] = 0x13; /* change +7 to +19 */
            executable = VirtualProtectFromApp(code, 4096, PAGE_EXECUTE_READ, &old);
            CHECK("patched code RW -> RX", executable);
            if (executable) {
                CHECK("flush patched instructions", FlushInstructionCache(GetCurrentProcess(), code, sizeof instructions));
                CHECK("patched f(35)=54", call_code(code, 35, &value, &exception) && value == 54);
                CHECK("patched f(-19)=0", call_code(code, -19, &value, &exception) && value == 0);
            }
        }
    }
    /* Observations, not requirements: the FromApp contract rejects RWX. */
    SetLastError(0);
    int rwx = VirtualProtectFromApp(code, 4096, PAGE_EXECUTE_READWRITE, &old);
    error = rwx ? 0 : GetLastError();
    LOG("FromApp protect RWX: %s (error=%lu)", rwx ? "accepted" : "rejected", error);
    CHECK("release code page", VirtualFree(code, 0, MEM_RELEASE));
    SetLastError(0);
    void *allocation = VirtualAllocFromApp(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    error = allocation ? 0 : GetLastError();
    LOG("FromApp allocate RWX: %s (error=%lu)", allocation ? "accepted" : "rejected", error);
    if (allocation) CHECK("release RWX page", VirtualFree(allocation, 0, MEM_RELEASE));

    SetLastError(0);
    code = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    error = code ? 0 : GetLastError();
    LOG("Desktop allocate RWX: %s (error=%lu)", code ? "accepted" : "rejected", error);
    if (code) {
        memcpy(code, instructions, sizeof instructions);
        CHECK("flush desktop RWX code", FlushInstructionCache(GetCurrentProcess(), code, sizeof instructions));
        CHECK("desktop RWX f(35)=42", call_code(code, 35, &value, &exception) && value == 42);
        DWORD previous;
        int protected = VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous);
        CHECK("desktop RWX -> RX", protected);
        if (protected) {
            protected = VirtualProtect(code, 4096, PAGE_EXECUTE_READWRITE, &previous);
            CHECK("desktop RX -> RWX", protected);
            if (protected) {
                code[2] = 0x13;
                CHECK("flush desktop RWX patch", FlushInstructionCache(GetCurrentProcess(), code, sizeof instructions));
                CHECK("desktop RWX patched f(35)=54", call_code(code, 35, &value, &exception) && value == 54);
            }
        }
        CHECK("release desktop RWX code", VirtualFree(code, 0, MEM_RELEASE));
    }
}

static volatile LONG fault_count;
static void *fault_page;
static DWORD fault_error;

static LONG CALLBACK recover_fault(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        record->NumberParameters < 2 || record->ExceptionInformation[0] != 0 ||
        record->ExceptionInformation[1] != (ULONG_PTR)fault_page)
        return EXCEPTION_CONTINUE_SEARCH;
    ULONG old;
    if (!VirtualProtectFromApp(fault_page, 4096, PAGE_READWRITE, &old)) {
        fault_error = GetLastError();
        return EXCEPTION_CONTINUE_SEARCH;
    }
    InterlockedIncrement(&fault_count);
    return EXCEPTION_CONTINUE_EXECUTION; /* Retry exactly the faulting load. */
}

__declspec(noinline) static int read_page(volatile int *page, int *value)
{
    __try {
        *value = *page;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static void page_faults(void)
{
    ULONG old;
    int value = 0;
    fault_page = VirtualAllocFromApp(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK("allocate fault page", fault_page != NULL);
    if (!fault_page) return;
    *(int *)fault_page = 0x13579;
    PVOID handler = AddVectoredExceptionHandler(1, recover_fault);
    CHECK("register vectored exception handler", handler != NULL);
    if (handler) {
        /* Re-arm twice: a handler which skips the load, or a one-shot success,
         * must not masquerade as safepoint/page-protection support. */
        for (int i = 1; i <= 2; ++i) {
            int protected = VirtualProtectFromApp(fault_page, 4096, PAGE_NOACCESS, &old);
            CHECK("arm PAGE_NOACCESS", protected);
            if (protected) {
                LOG("Triggering protected-page read %d...", i);
                CHECK("VEH resumes load with original value", read_page(fault_page, &value) &&
                    value == 0x13579 && fault_count == i);
            }
        }
        if (fault_error) LOG("  handler GetLastError=%lu", fault_error);
        CHECK("remove vectored handler", RemoveVectoredExceptionHandler(handler));
    }
    CHECK("release fault page", VirtualFree(fault_page, 0, MEM_RELEASE));
    fault_page = NULL;
}

typedef uintptr_t (*load_function)(void *);
static int unwind_fault(void *code, void *page, DWORD *exception)
{
    int expected = 0;
    __try {
        volatile uintptr_t value = ((load_function)code)(page);
        (void)value;
        return 0;
    } __except ((expected = GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION &&
        GetExceptionInformation()->ExceptionRecord->ExceptionAddress == (unsigned char *)code + 4 &&
        GetExceptionInformation()->ExceptionRecord->NumberParameters >= 2 &&
        GetExceptionInformation()->ExceptionRecord->ExceptionInformation[0] == 0 &&
        GetExceptionInformation()->ExceptionRecord->ExceptionInformation[1] == (ULONG_PTR)page),
        EXCEPTION_EXECUTE_HANDLER) {
        *exception = GetExceptionCode();
        return expected;
    }
}

static void dynamic_unwind(void)
{
    /* sub rsp,40; mov rax,[rcx]; add rsp,40; ret. Fault after the
     * prologue requires unwinding the dynamically registered stack frame. */
    const unsigned char instructions[] = { 0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x01,
        0x48, 0x83, 0xc4, 0x28, 0xc3 };
    /* UNWIND_INFO v1, prologue=4, one UWOP_ALLOC_SMALL(40) at offset 4. */
    const unsigned char unwind[] = { 1, 4, 1, 0, 4, 0x42, 0, 0 };
    unsigned char *code = VirtualAllocFromApp(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    void *page = VirtualAllocFromApp(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    ULONG old;
    CHECK("allocate unwind code and fault page", code && page);
    if (code && page) {
        memcpy(code, instructions, sizeof instructions);
        memcpy(code + 64, unwind, sizeof unwind);
        RUNTIME_FUNCTION table = { .BeginAddress = 0, .EndAddress = sizeof instructions, .UnwindData = 64 };
        int executable = VirtualProtectFromApp(code, 4096, PAGE_EXECUTE_READ, &old);
        CHECK("unwind test code RW -> RX", executable);
        if (executable) {
            CHECK("flush unwind test instructions", FlushInstructionCache(GetCurrentProcess(), code, sizeof instructions));
            int registered = RtlAddFunctionTable(&table, 1, (DWORD64)code);
            CHECK("RtlAddFunctionTable", registered);
            if (registered) {
                DWORD64 base = 0;
                PRUNTIME_FUNCTION found = RtlLookupFunctionEntry((DWORD64)(code + 4), &base, NULL);
                CHECK("lookup dynamic unwind entry", found && base == (DWORD64)code &&
                    found->BeginAddress == 0 && found->EndAddress == sizeof instructions && found->UnwindData == 64);
                DWORD exception = 0;
                LOG("Faulting inside generated non-leaf function...");
                CHECK("SEH unwinds generated frame", unwind_fault(code, page, &exception));
                LOG("  caught exception=0x%08lx", exception);
                CHECK("RtlDeleteFunctionTable", RtlDeleteFunctionTable(&table));
            }
        }
    }
    if (page) CHECK("release unwind fault page", VirtualFree(page, 0, MEM_RELEASE));
    if (code) CHECK("release unwind code page", VirtualFree(code, 0, MEM_RELEASE));
}

static DWORD tls_slot;
static unsigned __stdcall thread_entry(void *argument)
{
    if (TlsGetValue(tls_slot) != NULL || !TlsSetValue(tls_slot, argument)) return 1;
    return TlsGetValue(tls_slot) == argument ? 0 : 2;
}

static void threads(void)
{
    tls_slot = TlsAlloc();
    CHECK("TlsAlloc", tls_slot != TLS_OUT_OF_INDEXES);
    if (tls_slot == TLS_OUT_OF_INDEXES) return;
    CHECK("set main-thread TLS", TlsSetValue(tls_slot, (void *)(uintptr_t)0x1234));
    HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, thread_entry, (void *)(uintptr_t)0x5678, CREATE_SUSPENDED, NULL);
    CHECK("_beginthreadex suspended", thread != NULL);
    if (thread) {
        CHECK("ResumeThread", ResumeThread(thread) == 1);
        /* The worker only does TLS operations: wait to completion before
         * freeing its TLS slot, even if a slow machine exceeds expectations. */
        CHECK("join worker", WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
        DWORD result = 99;
        CHECK("worker TLS initially empty and independent", GetExitCodeThread(thread, &result) && result == 0);
        CHECK("main-thread TLS preserved", TlsGetValue(tls_slot) == (void *)(uintptr_t)0x1234);
        CHECK("close worker handle", CloseHandle(thread));
    }
    CHECK("TlsFree", TlsFree(tls_slot));
}

int sbcl_platform_probe(FILE *output)
{
    report = output;
    LOG("Manifest codeGeneration=%d / x64 MSVC UWP", PROBE_CODEGEN);
    generated_code();
    page_faults();
    dynamic_unwind();
    threads();
    LOG("COMPLETE: %d failed checks (not an SBCL compatibility verdict)", failures);
    return failures;
}

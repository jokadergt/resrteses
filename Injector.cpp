#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

#include "shellcode.h"

// change the shitty config here <3
namespace Config {
inline const char* DllPath = "Gaymodule.dll";

// ignore ts
inline const char* TargetProcess = "RobloxPlayerBeta.exe";

// You can dunno define console title here
inline const char* ConsoleTitle = "Im so gay for you";
} // namespace Config

namespace Nt {
using NtCreateSectionFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, void*, PLARGE_INTEGER, ULONG, ULONG, HANDLE);
using NtMapViewOfSectionFn = NTSTATUS(NTAPI*)(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, DWORD, ULONG, ULONG);
using NtUnmapViewOfSectionFn = NTSTATUS(NTAPI*)(HANDLE, PVOID);
using NtWriteVirtualMemoryFn = NTSTATUS(NTAPI*)(HANDLE, PVOID, PVOID, ULONG, PULONG);
using NtQuerySystemInformationFn = NTSTATUS(NTAPI*)(DWORD, PVOID, ULONG, PULONG);
using NtQueryObjectFn = NTSTATUS(NTAPI*)(HANDLE, DWORD, PVOID, ULONG, PULONG);
using ZwSetIoCompletionFn = NTSTATUS(NTAPI*)(HANDLE, PVOID, PVOID, NTSTATUS, ULONG_PTR);

inline HMODULE Ntdll() {
    static HMODULE h = GetModuleHandleA("ntdll.dll");
    return h;
}
inline NtCreateSectionFn CreateSection() {
    return (NtCreateSectionFn)GetProcAddress(Ntdll(), "NtCreateSection");
}
inline NtMapViewOfSectionFn MapView() {
    return (NtMapViewOfSectionFn)GetProcAddress(Ntdll(), "NtMapViewOfSection");
}
inline NtUnmapViewOfSectionFn UnmapView() {
    return (NtUnmapViewOfSectionFn)GetProcAddress(Ntdll(), "NtUnmapViewOfSection");
}
inline NtWriteVirtualMemoryFn WriteVm() {
    return (NtWriteVirtualMemoryFn)GetProcAddress(Ntdll(), "NtWriteVirtualMemory");
}
} // namespace Nt

static void PrintLine(const char* s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || !h) return;
    DWORD written = 0;
    DWORD len = (DWORD)strlen(s);
    WriteConsoleA(h, s, len, &written, nullptr);
    WriteConsoleA(h, "\n", 1, &written, nullptr);
}

static void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    strcat_s(buf, "\n");
    HANDLE f = CreateFileA("injector.log", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(f, buf, (DWORD)strlen(buf), &w, nullptr);
        CloseHandle(f);
    }
    PrintLine(buf);
}


static DWORD FindPid(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static void SuspendAll(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE th = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (th) {
                    SuspendThread(th);
                    CloseHandle(th);
                }
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static void ResumeAll(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE th = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (th) {
                    ResumeThread(th);
                    CloseHandle(th);
                }
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static bool ReadFileToMemory(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)size);
    return (bool)f.read((char*)out.data(), size);
}

static std::string PickDecoyDll() {
    auto NtCreateSection = Nt::CreateSection();
    auto NtMapView = Nt::MapView();
    auto NtUnmap = Nt::UnmapView();
    std::vector<std::string> candidates;
    if (!NtCreateSection || !NtMapView || !NtUnmap) return {};

    WIN32_FIND_DATAA fd{};
    HANDLE hFind = FindFirstFileA("C:\\Windows\\System32\\*.dll", &fd);
    if (hFind == INVALID_HANDLE_VALUE) return {};
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        uint64_t fsize = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (fsize == 0 || fsize > 0x10000) continue;

        std::string full = std::string("C:\\Windows\\System32\\") + fd.cFileName;
        HANDLE hFile = CreateFileA(full.c_str(), GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) continue;

        HANDLE hSec = nullptr;
        NTSTATUS st = NtCreateSection(&hSec, 0xF001F, nullptr, nullptr, 2, 0x1000000, hFile);
        CloseHandle(hFile);
        if (st < 0) continue;

        PVOID base = nullptr;
        SIZE_T viewSize = 0;
        HANDLE cur = GetCurrentProcess();
        st = NtMapView(hSec, cur, &base, 0, 0, nullptr, &viewSize, 2, 0, 2);
        CloseHandle(hSec);
        if (st >= 0) {
            bool ok = (viewSize == 0x10000);
            NtUnmap(cur, base);
            if (ok) candidates.push_back(full);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    if (candidates.empty()) return {};
    srand(GetTickCount());
    return candidates[rand() % candidates.size()];
}

static bool WriteRemoteSplit(HANDLE hProc, uint64_t remote, const uint8_t* local, size_t size) {
    auto NtWrite = Nt::WriteVm();
    if (!NtWrite) return false;
    size_t off = 0;
    while (off < size) {
        uint64_t cur = remote + off;
        size_t chunk = 0x10000 - (size_t)(cur & 0xFFFF);
        if (chunk > size - off) chunk = size - off;
        ULONG written = 0;
        NTSTATUS st = NtWrite(hProc, (PVOID)cur, (PVOID)(local + off), (ULONG)chunk, &written);
        if (st < 0 || written != chunk) return false;
        off += chunk;
    }
    return true;
}

static uint64_t MapImageInTarget(HANDLE hProc, const std::vector<uint8_t>& dll, const std::string& decoy, uint64_t* outBase) {
    *outBase = 0;
    if (dll.size() < sizeof(IMAGE_DOS_HEADER)) return 0;
    auto dos = (const IMAGE_DOS_HEADER*)dll.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > dll.size()) return 0;
    auto nt = (const IMAGE_NT_HEADERS64*)(dll.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;

    DWORD sizeOfImage = nt->OptionalHeader.SizeOfImage;
    if (!sizeOfImage) return 0;

    auto NtCreateSection = Nt::CreateSection();
    auto NtMapView = Nt::MapView();
    auto NtWrite = Nt::WriteVm();
    if (!NtCreateSection || !NtMapView || !NtWrite) return 0;

    char tmpPath[MAX_PATH]{}, tmpFile[MAX_PATH]{};
    GetTempPathA(sizeof(tmpPath), tmpPath);
    GetTempFileNameA(tmpPath, "BYF", 0, tmpFile);
    if (!CopyFileA(decoy.c_str(), tmpFile, FALSE)) {
        DeleteFileA(tmpFile);
        return 0;
    }
    HANDLE hDecoy = CreateFileA(tmpFile, 0xA0000000, 5,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hDecoy == INVALID_HANDLE_VALUE) {
        DeleteFileA(tmpFile);
        return 0;
    }

    DWORD chunks = (DWORD)(((sizeOfImage + 0xFFFF) >> 16) + 1);

    uint64_t remoteBase = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQueryEx(hProc, (LPCVOID)0x10000, &mbi, sizeof(mbi)) == 0) {
        CloseHandle(hDecoy);
        DeleteFileA(tmpFile);
        return 0;
    }
    while (true) {
        uint64_t regionBase = (uint64_t)mbi.BaseAddress;
        uint64_t regionEnd = regionBase + mbi.RegionSize;
        if (mbi.State == MEM_FREE) {
            uint64_t aligned = (regionBase + 0xFFFF) & ~0xFFFFULL;
            if (aligned < 0x10000) aligned = 0x10000;
            uint64_t need = (uint64_t)chunks << 16;
            if (aligned + need <= regionEnd) {
                remoteBase = aligned;
                break;
            }
        }
        if (regionEnd >= 0x7FFFFFFFFFFEULL) break;
        if (VirtualQueryEx(hProc, (LPCVOID)regionEnd, &mbi, sizeof(mbi)) == 0) break;
    }
    if (!remoteBase) {
        CloseHandle(hDecoy);
        DeleteFileA(tmpFile);
        return 0;
    }

    for (DWORD i = 0; i < chunks; i++) {
        HANDLE hSec = nullptr;
        NTSTATUS st = NtCreateSection(&hSec, 0xF001F, nullptr, nullptr, 2, 0x1000000, hDecoy);
        if (st < 0) continue;
        PVOID addr = (PVOID)(remoteBase + ((uint64_t)i << 16));
        SIZE_T vsz = 0;
        st = NtMapView(hSec, hProc, &addr, 0, 0, nullptr, &vsz, 2, 0, 32);
        CloseHandle(hSec);
        if (st < 0) continue;
        DWORD old = 0;
        VirtualProtectEx(hProc, addr, 0x10000, PAGE_EXECUTE_READWRITE, &old);
        static uint8_t zero[0x10000]{};
        ULONG done = 0;
        NtWrite(hProc, addr, zero, 0x10000, &done);
    }
    CloseHandle(hDecoy);

    auto sect = IMAGE_FIRST_SECTION(nt);
    DWORD hdrSize = nt->OptionalHeader.SizeOfHeaders;
    if (hdrSize > dll.size()) hdrSize = (DWORD)dll.size();
    if (!WriteRemoteSplit(hProc, remoteBase, dll.data(), hdrSize)) {
        DeleteFileA(tmpFile);
        return 0;
    }
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD rawSize = sect[i].SizeOfRawData;
        if (!rawSize) continue;
        DWORD rawPtr = sect[i].PointerToRawData;
        if ((uint64_t)rawPtr + rawSize > dll.size()) continue;
        uint64_t dst = remoteBase + sect[i].VirtualAddress;
        if (!WriteRemoteSplit(hProc, dst, dll.data() + rawPtr, rawSize)) {
            DeleteFileA(tmpFile);
            return 0;
        }
    }

    {
        using CreateTransactionFn = HANDLE(WINAPI*)(void*, void*, DWORD, DWORD, DWORD, DWORD, void*);
        using DeleteTransactedFn = BOOL(WINAPI*)(LPCSTR, HANDLE);
        using CommitFn = BOOL(WINAPI*)(HANDLE);
        HMODULE ktm = LoadLibraryA("ktmw32.dll");
        if (ktm) {
            auto pCreate = (CreateTransactionFn)GetProcAddress(ktm, "CreateTransaction");
            auto pDel = (DeleteTransactedFn)GetProcAddress(ktm, "DeleteFileTransactedA");
            auto pCommit = (CommitFn)GetProcAddress(ktm, "CommitTransaction");
            if (pCreate && pDel && pCommit) {
                HANDLE tx = pCreate(nullptr, nullptr, 0, 0, 0, 0, nullptr);
                if (tx && tx != INVALID_HANDLE_VALUE) {
                    pDel(tmpFile, tx);
                    pCommit(tx);
                    CloseHandle(tx);
                } else {
                    DeleteFileA(tmpFile);
                }
            } else {
                DeleteFileA(tmpFile);
            }
        } else {
            DeleteFileA(tmpFile);
        }
    }

    *outBase = remoteBase;
    return remoteBase;
}

static bool IoCompletionHijack(HANDLE hProc, void* stubRemote) {
    HMODULE ntdll = Nt::Ntdll();
    auto NtQSI = (Nt::NtQuerySystemInformationFn)GetProcAddress(ntdll, "NtQuerySystemInformation");
    auto NtQO = (Nt::NtQueryObjectFn)GetProcAddress(ntdll, "NtQueryObject");
    auto ZwSetIo = (Nt::ZwSetIoCompletionFn)GetProcAddress(ntdll, "ZwSetIoCompletion");
    if (!NtQSI || !NtQO || !ZwSetIo) return false;

    std::vector<uint8_t> info(100000);
    LONG qsiSt = NtQSI(51, info.data(), (ULONG)info.size(), nullptr);
    uint64_t count = 0;
    if (qsiSt >= 0) {
        count = *(uint64_t*)info.data();
    }
    if (qsiSt < 0 || count == 0) {
        LONG st16 = NtQSI(16, info.data(), (ULONG)info.size(), nullptr);
        uint32_t c16 = (st16 >= 0) ? *(uint32_t*)info.data() : 0;
        if (st16 >= 0 && c16 && c16 < 0x100000) {
            count = c16;
            qsiSt = st16;
        }
    }

    auto IsIoCompletion = [&](HANDLE dup) -> bool {
        uint8_t objInfo[0x2710]{};
        if (NtQO(dup, 2 /*ObjectTypeInformation*/, objInfo, sizeof(objInfo), nullptr) < 0)
            return false;
        wchar_t* buf = *(wchar_t**)(objInfo + 8);
        if (!buf) return false;
        __try {
            static const wchar_t want[] = L"IoCompletion";
            for (int i = 0; i < 13; i++) {
                wchar_t a = buf[i], b = (i < 12 ? want[i] : 0);
                if (a != b) return false;
            }
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    };

    HANDLE hIo = nullptr;
    if (count > 0 && count < 0x100000) {
        for (uint64_t h = 0; h < count; h++) {
            HANDLE dup = nullptr;
            if (!DuplicateHandle(hProc, (HANDLE)h, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
                continue;
            if (IsIoCompletion(dup)) { hIo = dup; break; }
            CloseHandle(dup);
        }
    }
    if (!hIo) {
        Log("[*] scanning brute-force handles for IoCompletion");
        for (uint64_t h = 0; h < 0x10000; h += 4) {
            HANDLE dup = nullptr;
            if (!DuplicateHandle(hProc, (HANDLE)h, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
                continue;
            if (IsIoCompletion(dup)) { hIo = dup; break; }
            CloseHandle(dup);
        }
    }
    if (!hIo) {
        Log("[-] no IoCompletion handle found");
        return false;
    }
    Log("[+] IoCompletion handle found, scanning for cave");

    bool queued = false;
    MEMORY_BASIC_INFORMATION mbi{};
    uint64_t cursor = 0;
    uint8_t page[4096];
#pragma pack(push, 1)
    struct CavePkt {
        uint64_t f0;      // +0
        uint64_t f1;      // +8
        uint8_t z1[32];   // +16..+47
        uint64_t z2;      // +48
        uint64_t stub;    // +56
        uint64_t z3;      // +64
    };
#pragma pack(pop)
    static_assert(sizeof(CavePkt) == 0x48, "cave pkt must be 0x48");
    CavePkt pkt{};
    pkt.stub = (uint64_t)stubRemote;

    while (VirtualQueryEx(hProc, (LPCVOID)cursor, &mbi, sizeof(mbi))) {
        uint64_t regionBase = (uint64_t)mbi.BaseAddress;
        uint64_t regionEnd = regionBase + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE && mbi.RegionSize >= 0x48) {
            for (uint64_t off = 0; off + 0x48 <= mbi.RegionSize; off += 0x1000) {
                uint64_t addr = regionBase + off;
                SIZE_T remain = mbi.RegionSize - off;
                if (remain < 0x48) break;
                SIZE_T toRead = remain - 0x47;
                if (toRead > sizeof(page)) toRead = sizeof(page);
                SIZE_T rd = 0;
                SIZE_T readReq = toRead + 0x47;
                if (readReq > sizeof(page)) readReq = sizeof(page);
                if (!ReadProcessMemory(hProc, (LPCVOID)addr, page, readReq, &rd) || rd < 0x48) continue;
                for (size_t k = 0; k + 0x48 <= (size_t)rd; k++) {
                    bool allZero = true;
                    for (int z = 0; z < 0x48; z++) if (page[k + z] != 0) { allZero = false; break; }
                    if (!allZero) continue;
                    uint64_t cave = addr + k;
                    if (!cave) continue;
                    SIZE_T wr = 0;
                    if (WriteProcessMemory(hProc, (LPVOID)cave, &pkt, sizeof(pkt), &wr) && wr == sizeof(pkt)) {
                        ZwSetIo(hIo, (PVOID)cave, nullptr, 0, 0);
                        queued = true;
                        break;
                    }
                }
                if (queued) break;
            }
        }
        if (queued) break;
        if (regionEnd >= 0x7FFFFFFFFFFEULL) break;
        cursor = regionEnd;
        if (cursor == 0) break;
    }
    CloseHandle(hIo);
    Log("[%s] cave queue %s", queued ? "+" : "-", queued ? "ok" : "failed");
    return queued;
}

static bool TriggerShellcode(HANDLE hProc, uint64_t remoteDllBase) {
    auto NtCreateSection = Nt::CreateSection();
    auto NtMapView = Nt::MapView();
    if (!NtCreateSection || !NtMapView) { Log("[-] TriggerShellcode: no Nt funcs"); return false; }

    HANDLE hSec = nullptr;
    int64_t secSize = 0x1020; // 4128
    NTSTATUS st = NtCreateSection(&hSec, 14, nullptr, (PLARGE_INTEGER)&secSize,
                                  PAGE_EXECUTE_READWRITE /*64*/, SEC_COMMIT /*0x8000000*/, nullptr);
    if (st < 0) { Log("[-] NtCreateSection failed st=%08X", st); return false; }

    PVOID remote = nullptr;
    SIZE_T viewSize = 0x1020;
    st = NtMapView(hSec, hProc, &remote, 0, 0, nullptr, &viewSize, 2 /*ViewShare*/, 0, PAGE_EXECUTE_READWRITE);
    if (st < 0) {
        Log("[-] NtMapView failed st=%08X", st);
        CloseHandle(hSec);
        return false;
    }
    uint64_t remoteBase = (uint64_t)remote;
    uint64_t paramAddr = remoteBase + 0x1000;
    Log("[+] shellcode section at %llx", remoteBase);

    SIZE_T wr = 0;
    if (!WriteProcessMemory(hProc, remote, (LPCVOID)kLoaderShellcode, 0x1000, nullptr)) {
        Log("[-] WriteProcessMemory shellcode failed err=%lu", GetLastError());
        CloseHandle(hSec);
        return false;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    FARPROC pLoad = GetProcAddress(k32, "LoadLibraryA");
    FARPROC pGetProc = GetProcAddress(k32, "GetProcAddress");
    struct Params { uint64_t dllBase; uint64_t load; uint64_t getproc; uint64_t zero; };
    Params p{ remoteDllBase, (uint64_t)pLoad, (uint64_t)pGetProc, 0 };
    if (!WriteProcessMemory(hProc, (LPVOID)paramAddr, &p, sizeof(p), &wr) || wr != sizeof(p)) {
        Log("[-] WriteProcessMemory params failed");
        CloseHandle(hSec);
        return false;
    }

    uint8_t stub[22];
    stub[0] = 0x48; stub[1] = 0xB8;
    memcpy(stub + 2, &remoteBase, 8);
    stub[10] = 0x48; stub[11] = 0xB9;
    memcpy(stub + 12, &paramAddr, 8);
    stub[20] = 0xFF; stub[21] = 0xE0;

    void* stubRemote = VirtualAllocEx(hProc, nullptr, sizeof(stub), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stubRemote || !WriteProcessMemory(hProc, stubRemote, stub, sizeof(stub), &wr)) {
        Log("[-] stub alloc/write failed err=%lu", GetLastError());
        CloseHandle(hSec);
        return false;
    }
    Log("[+] stub at %p", stubRemote);

    if (!IoCompletionHijack(hProc, stubRemote)) {
        Log("[-] IoCompletionHijack failed");
        CloseHandle(hSec);
        return false;
    }

    DWORD last = 0;
    for (int i = 0; i < 40; i++) {
        Sleep(500);
        uint8_t back[0x20]{};
        SIZE_T rd = 0;
        if (ReadProcessMemory(hProc, (LPCVOID)paramAddr, back, sizeof(back), &rd)) {
            DWORD status = *(DWORD*)(back + 0x18);
            if (status != last) {
                Log("[*] shellcode status -> %lu", status);
                last = status;
            }
            if (status == 5) {
                CloseHandle(hSec);
                return true;
            }
        } else {
            Log("[-] status poll read failed err=%lu", GetLastError());
            break;
        }
    }
    Log("[-] shellcode did not reach status 5 (last=%lu)", last);
    CloseHandle(hSec);
    return false;
}

int main() {
    AllocConsole();
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    SetStdHandle(STD_OUTPUT_HANDLE, hOut);
    SetStdHandle(STD_ERROR_HANDLE, hOut);
    SetStdHandle(STD_INPUT_HANDLE, hIn);
    SetConsoleTitleA(Config::ConsoleTitle);
    DWORD mode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode))
        SetConsoleMode(hOut, mode | 4);

    BOOL elevated = FALSE;
    HANDLE hTok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hTok)) {
        TOKEN_ELEVATION elev{};
        DWORD sz = 0;
        if (GetTokenInformation(hTok, TokenElevation, &elev, sizeof(elev), &sz))
            elevated = elev.TokenIsElevated;
        CloseHandle(hTok);
    }
    Log("[*] start, pid=%lu elevated=%d", GetCurrentProcessId(), elevated);

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        Log("[-] no ntdll");
        Sleep(800);
        return 0;
    }

    DWORD pid = FindPid(Config::TargetProcess);
    if (!pid) {
        Log("[-] no target process %s", Config::TargetProcess);
        Sleep(800);
        return 0;
    }
    Log("[+] target pid=%lu", pid);

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        Log("[-] OpenProcess failed err=%lu", GetLastError());
        Sleep(800);
        return 0;
    }
    Log("[+] opened process handle");

    std::vector<uint8_t> dll;
    if (!ReadFileToMemory(Config::DllPath, dll) || dll.empty()) {
        Log("[-] failed to read %s err=%lu", Config::DllPath, GetLastError());
        Sleep(1000);
        CloseHandle(hProc);
        return 0;
    }
    Log("[+] read dll %zu bytes", dll.size());

    std::string decoy = PickDecoyDll();
    if (decoy.empty()) {
        Log("[-] no decoy dll");
        Sleep(1000);
        CloseHandle(hProc);
        return 0;
    }
    Log("[+] decoy=%s", decoy.c_str());

    SuspendAll(pid);
    Log("[+] threads suspended");

    uint64_t remoteBase = 0;
    uint64_t mapped = MapImageInTarget(hProc, dll, decoy, &remoteBase);
    Log("[*] MapImageInTarget returned %llx base=%llx", mapped, remoteBase);

    ResumeAll(pid);
    Log("[+] threads resumed");

    bool ok = false;
    if (mapped && remoteBase) {
        ok = TriggerShellcode(hProc, remoteBase);
        Log("[*] TriggerShellcode returned %d", ok);
    } else {
        Log("[-] mapping failed");
    }

    if (ok) {
        Log("[+] injected");
    } else {
        Log("[-] failed");
        Sleep(1000);
    }

    CloseHandle(hProc);
    return 0;
}

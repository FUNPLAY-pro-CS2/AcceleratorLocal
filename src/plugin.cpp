/**
 * ======================================================
 * Accelerator local
 * Written by Slynx (˙·٠● S l y n x ●٠·˙) 2026, Phoenix (˙·٠●Феникс●٠·˙) 2023-2025, Asher Baker (asherkin) 2011.
 * ======================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from 
 * the use of this software.
*/

#define VERSION_STRING SEMVER " @ " GITHUB_SHA
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

#include "plugin.h"

#include "CMiniDumpComment.hpp"
#include "httpmanager.h"

#include "filesystem.h"
#include <tier1/KeyValues.h>

#include "client/linux/handler/exception_handler.h"
#include "common/linux/linux_libc_support.h"
#include "third_party/lss/linux_syscall_support.h"

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <limits>

#include "common/path_helper.h"
#include "common/using_std_string.h"
#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"
#include <google_breakpad/processor/call_stack.h>
#include <google_breakpad/processor/stack_frame.h>
#include <processor/pathname_stripper.h>
#include "common/linux/dump_symbols.h"

#include <sstream>

Plugin g_Plugin;
PLUGIN_EXPOSE(AcceleratorLocal, g_Plugin);

HTTPManager g_httpManager;
CSteamGameServerAPIContext g_steamAPI;
ISteamHTTP* g_pSteamHttp = nullptr;

char g_szCrashMap[256];
char g_szCrashGamePath[512];
char g_szCrashCommandLine[1024];
char g_szDumpStoragePath[512];

char g_szDiscordWebhook[512];
char g_szPendingCrashPath[512];
char g_szSessionStatePath[512];

google_breakpad::ExceptionHandler* g_pExceptionHandler = nullptr;
CMiniDumpComment g_MiniDumpComment(95000);

void (*SignalHandler)(int, siginfo_t*, void*);
const int kExceptionSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
const int kNumHandledSignals = std::size(kExceptionSignals);

class GameSessionConfiguration_t
{
};

SH_DECL_HOOK3_void(ISource2Server, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK0_void(ISource2Server, GameServerSteamAPIActivated, SH_NOATTRIB, 0);
SH_DECL_HOOK0_void(ISource2Server, GameServerSteamAPIDeactivated, SH_NOATTRIB, 0);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);

static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded)
{
    if (succeeded)
        sys_write(STDOUT_FILENO, "Wrote minidump to: ", 19);
    else
        sys_write(STDOUT_FILENO, "Failed to write minidump to: ", 29);

    sys_write(STDOUT_FILENO, descriptor.path(), my_strlen(descriptor.path()));
    sys_write(STDOUT_FILENO, "\n", 1);

    if (!succeeded)
        return succeeded;

    // Leave a marker so the next server start knows there is an unprocessed crash.
    int pending = sys_open(g_szPendingCrashPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (pending != -1)
    {
        sys_write(pending, descriptor.path(), my_strlen(descriptor.path()));
        sys_close(pending);
    }

    my_strlcpy(g_szDumpStoragePath, descriptor.path(), sizeof(g_szDumpStoragePath));
    my_strlcat(g_szDumpStoragePath, ".txt", sizeof(g_szDumpStoragePath));

    int extra = sys_open(g_szDumpStoragePath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if (extra == -1)
    {
        sys_write(STDOUT_FILENO, "Failed to open metadata file!\n", 30);
        return succeeded;
    }

    sys_write(extra, "-------- CONFIG BEGIN --------", 30);
    sys_write(extra, "\nMap=", 5);
    sys_write(extra, g_szCrashMap, my_strlen(g_szCrashMap));
    sys_write(extra, "\nGamePath=", 10);
    sys_write(extra, g_szCrashGamePath, my_strlen(g_szCrashGamePath));
    sys_write(extra, "\nCommandLine=", 13);
    sys_write(extra, g_szCrashCommandLine, my_strlen(g_szCrashCommandLine));
    sys_write(extra, "\n-------- CONFIG END --------\n", 30);
    sys_write(extra, "\n", 1);

    LoggingSystem_GetLogCapture(&g_MiniDumpComment, true);
    const char* pszConsoleHistory = g_MiniDumpComment.GetStartPointer();

    if (pszConsoleHistory[0])
    {
        sys_write(extra, "-------- CONSOLE HISTORY BEGIN --------\n", 40);
        sys_write(extra, pszConsoleHistory, my_strlen(pszConsoleHistory));
        sys_write(extra, "-------- CONSOLE HISTORY END --------\n", 38);
        sys_write(extra, "\n", 1);
    }

    std::shared_ptr<google_breakpad::SimpleSymbolSupplier> symbolSupplier;
    google_breakpad::BasicSourceLineResolver resolver;
    google_breakpad::MinidumpProcessor minidump_processor(symbolSupplier.get(), &resolver);

    // Increase the maximum number of threads and regions.
    google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
    google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());
    // Process the minidump.
    google_breakpad::Minidump miniDump(descriptor.path());
    if (!miniDump.Read())
    {
        sys_write(STDOUT_FILENO, "Failed to read minidump\n", 24);
    }
    else
    {
        google_breakpad::ProcessState processState;
        if (minidump_processor.Process(&miniDump, &processState) != google_breakpad::PROCESS_OK)
        {
            sys_write(STDOUT_FILENO, "MinidumpProcessor::Process failed\n", 34);
        }
        else
        {
            int requestingThread = processState.requesting_thread();
            if (requestingThread == -1)
                requestingThread = 0;

            const google_breakpad::CallStack* stack = processState.threads()->at(requestingThread);
            size_t frameCount = MIN(stack->frames()->size(), 15);

            auto signal_safe_hex_print = [](uint64_t num)
            {
                char buffer[18];
                char* ptr = buffer + sizeof(buffer);

                if (num == 0)
                    *(--ptr) = '0';
                else
                {
                    while (num > 0)
                    {
                        *(--ptr) = "0123456789abcdef"[num % 16];
                        num /= 16;
                    }
                }

                *(--ptr) = 'x';
                *(--ptr) = '0';

                size_t length = buffer + sizeof(buffer) - ptr;
                sys_write(STDOUT_FILENO, ptr, length);
            };

            sys_write(STDOUT_FILENO, "\n", 1);
            for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
            {
                const google_breakpad::StackFrame* frame = stack->frames()->at(frameIndex);

                uint64_t moduleOffset = frame->ReturnAddress();
                if (frame->module)
                {
                    const std::string moduleFile = google_breakpad::PathnameStripper::File(frame->module->code_file());
                    moduleOffset -= frame->module->base_address();
                    sys_write(STDOUT_FILENO, moduleFile.c_str(), moduleFile.size());
                    sys_write(STDOUT_FILENO, " + ", 3);
                    signal_safe_hex_print(moduleOffset);
                    sys_write(STDOUT_FILENO, "\n", 1);
                }
                else
                {
                    sys_write(STDOUT_FILENO, "unknown + ", 10);
                    signal_safe_hex_print(moduleOffset);
                    sys_write(STDOUT_FILENO, "\n", 1);
                }
            }

            freopen(g_szDumpStoragePath, "a", stdout);
            PrintProcessState(processState, true, false, &resolver);
            fflush(stdout);
        }
    }

    sys_close(extra);

    return succeeded;
}

static void WriteSessionState(const char* pszState)
{
    FILE* file = fopen(g_szSessionStatePath, "w");
    if (file)
    {
        fputs(pszState, file);
        fclose(file);
    }
}

static bool RunCommandCapture(const char* pszCommand, char* pszOut, size_t maxlen)
{
    pszOut[0] = '\0';

    FILE* pipe = popen(pszCommand, "r");
    if (!pipe)
        return false;

    size_t total = fread(pszOut, 1, maxlen - 1, pipe);
    pszOut[total] = '\0';

    return pclose(pipe) == 0 && total > 0;
}

// Paths end up inside single quotes in a shell command, so refuse anything that could break out.
static bool IsShellSafe(const char* psz)
{
    for (; *psz; ++psz)
    {
        if (*psz == '\'' || *psz == '\\' || *psz == '\n' || *psz == '\r')
            return false;
    }

    return true;
}

// Returns "llvm-symbolizer", "addr2line" or nullptr depending on what is available on the machine.
static const char* FindSymbolizerTool()
{
    static char szTool[32];

    char szOut[256];
    if (RunCommandCapture("command -v llvm-symbolizer 2>/dev/null", szOut, sizeof(szOut)))
    {
        strncpy(szTool, "llvm-symbolizer", sizeof(szTool) - 1);
        return szTool;
    }

    if (RunCommandCapture("command -v addr2line 2>/dev/null", szOut, sizeof(szOut)))
    {
        strncpy(szTool, "addr2line", sizeof(szTool) - 1);
        return szTool;
    }

    return nullptr;
}

static bool SymbolizeAddress(const char* pszTool, const char* pszModulePath, uint64_t offset, char* pszOut, size_t maxlen)
{
    pszOut[0] = '\0';

    if (!pszTool || !IsShellSafe(pszModulePath) || access(pszModulePath, R_OK) != 0)
        return false;

    char szCommand[1024];
    if (strcmp(pszTool, "llvm-symbolizer") == 0)
        snprintf(szCommand, sizeof(szCommand), "llvm-symbolizer --obj='%s' 0x%lx 2>/dev/null", pszModulePath, offset);
    else
        snprintf(szCommand, sizeof(szCommand), "addr2line -f -C -e '%s' 0x%lx 2>/dev/null", pszModulePath, offset);

    char szOutput[1024];
    if (!RunCommandCapture(szCommand, szOutput, sizeof(szOutput)))
        return false;

    // First line is the function name, second line is file:line.
    char* pszFunction = szOutput;
    char* pszLocation = strchr(szOutput, '\n');
    if (pszLocation)
    {
        *pszLocation++ = '\0';
        char* pszEnd = strchr(pszLocation, '\n');
        if (pszEnd)
            *pszEnd = '\0';
    }

    if (!pszFunction[0] || strcmp(pszFunction, "??") == 0)
        return false;

    if (pszLocation && pszLocation[0] && strncmp(pszLocation, "??", 2) != 0)
        snprintf(pszOut, maxlen, "%s @ %s", pszFunction, pszLocation);
    else
        snprintf(pszOut, maxlen, "%s", pszFunction);

    return true;
}

// The crash is processed during plugin load, long before the Steam API activates,
// so the finished report body waits here until Hook_GameServerSteamAPIActivated fires.
std::string g_strPendingDiscordBody;
std::string g_strPendingDiscordContentType;

static void FlushPendingDiscordReport()
{
    if (g_strPendingDiscordBody.empty() || !g_pSteamHttp)
        return;

    HTTPManager::GenerateRequestOverride(
        k_EHTTPMethodPOST, g_szDiscordWebhook,
        (uint8*)g_strPendingDiscordBody.data(), (int)g_strPendingDiscordBody.size(),
        g_strPendingDiscordContentType.c_str(),
        [](HTTPRequestHandle, json)
        {
            META_LOG(&g_Plugin, "g_pExceptionHandlerCrash report sent to Discord.\n");
        },
        [](HTTPRequestHandle, EHTTPStatusCode statusCode, json)
        {
            META_LOG(&g_Plugin, "g_pExceptionHandlerDiscord webhook failed (HTTP %d).\n", statusCode);
        },
        nullptr);

    g_strPendingDiscordBody.clear();
    g_strPendingDiscordContentType.clear();
}

static void SendDiscordReport(const char* pszReport, const char* pszTxtPath)
{
    if (!g_szDiscordWebhook[0])
        return;

    // Discord message limit is 2000 characters, keep some headroom for the code fences.
    std::string content(pszReport);
    if (content.size() > 1800)
        content.resize(1800);

    json payload;
    payload["content"] = "```\n" + content + "\n```";

    std::string txtData;
    if (pszTxtPath)
    {
        FILE* file = fopen(pszTxtPath, "rb");
        if (file)
        {
            char buffer[8192];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
                txtData.append(buffer, bytes);
            fclose(file);
        }
    }

    if (!txtData.empty())
    {
        // Discord needs multipart/form-data for attachments and Steam HTTP only takes
        // a raw body, so the multipart body is assembled by hand.
        const char* pszBoundary = "AcceleratorLocalBoundary7MA4YWxkTrZu0gW";

        std::string body;
        body += "--"; body += pszBoundary; body += "\r\n";
        body += "Content-Disposition: form-data; name=\"payload_json\"\r\n";
        body += "Content-Type: application/json\r\n\r\n";
        body += payload.dump();
        body += "\r\n--"; body += pszBoundary; body += "\r\n";
        body += "Content-Disposition: form-data; name=\"files[0]\"; filename=\"crash.txt\"\r\n";
        body += "Content-Type: text/plain\r\n\r\n";
        body += txtData;
        body += "\r\n--"; body += pszBoundary; body += "--\r\n";

        g_strPendingDiscordBody = std::move(body);
        g_strPendingDiscordContentType = std::string("multipart/form-data; boundary=") + pszBoundary;
    }
    else
    {
        g_strPendingDiscordBody = payload.dump();
        g_strPendingDiscordContentType = "application/json";
    }

    if (g_pSteamHttp)
        FlushPendingDiscordReport();
    else
        META_LOG(&g_Plugin, "g_pExceptionHandlerCrash report queued, will be sent to Discord once the Steam API activates.\n");
}

static void ProcessPendingCrash(bool bPrevSessionStarted)
{
    char szDumpPath[512] = {};

    FILE* file = fopen(g_szPendingCrashPath, "r");
    if (!file)
        return;

    size_t total = fread(szDumpPath, 1, sizeof(szDumpPath) - 1, file);
    fclose(file);
    szDumpPath[total] = '\0';

    // Remove the marker first so a crash below can never loop forever.
    unlink(g_szPendingCrashPath);

    if (!szDumpPath[0])
        return;

    if (!bPrevSessionStarted)
    {
        META_LOG(&g_Plugin, "g_pExceptionHandlerCrash dump %s left unprocessed: previous session crashed before the server finished starting (crash loop protection).\n", szDumpPath);
        return;
    }

    META_LOG(&g_Plugin, "g_pExceptionHandlerServer crashed last session, processing %s\n", szDumpPath);

    google_breakpad::BasicSourceLineResolver resolver;
    google_breakpad::MinidumpProcessor processor(nullptr, &resolver);
    google_breakpad::Minidump miniDump(szDumpPath);

    google_breakpad::ProcessState processState;
    if (!miniDump.Read() || processor.Process(&miniDump, &processState) != google_breakpad::PROCESS_OK)
    {
        META_LOG(&g_Plugin, "g_pExceptionHandlerFailed to process the crash dump.\n");
        return;
    }

    int requestingThread = processState.requesting_thread();
    if (requestingThread == -1)
        requestingThread = 0;

    const google_breakpad::CallStack* stack = processState.threads()->at(requestingThread);
    size_t frameCount = MIN(stack->frames()->size(), 10);

    // Symbolize third-party (addons/) modules in-process with the bundled breakpad:
    // symbols are extracted straight from the ELF on disk, so nothing besides this
    // plugin is needed inside the steamrt container.
    // Breakpad's demangler predates C++20 mangling and floods stderr with thousands of
    // "failed to demangle" warnings on modern binaries, so stderr is muted while dumping.
    int savedStderr = dup(STDERR_FILENO);
    int devNull = open("/dev/null", O_WRONLY);
    if (devNull != -1)
        dup2(devNull, STDERR_FILENO);

    for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        google_breakpad::StackFrame* frame = stack->frames()->at(frameIndex);
        if (!frame->module || resolver.HasModule(frame->module))
            continue;

        const std::string modulePath = frame->module->code_file();
        if (modulePath.find("/addons/") == std::string::npos || access(modulePath.c_str(), R_OK) != 0)
            continue;

        std::ostringstream symbolStream;
        google_breakpad::DumpOptions options(SYMBOLS_AND_FILES, true, false, false);
        if (google_breakpad::WriteSymbolFile(modulePath, modulePath, "Linux", "", std::vector<string>(), options, symbolStream))
            resolver.LoadModuleUsingMapBuffer(frame->module, symbolStream.str());
    }

    if (devNull != -1)
    {
        if (savedStderr != -1)
            dup2(savedStderr, STDERR_FILENO);
        close(devNull);
    }
    if (savedStderr != -1)
        close(savedStderr);

    // External tools stay as a fallback for modules the in-process pass couldn't handle.
    const char* pszTool = FindSymbolizerTool();

    char szReport[3072];
    snprintf(szReport, sizeof(szReport), "Server crashed: %s @ 0x%lx\n\n",
             processState.crash_reason().c_str(), processState.crash_address());

    char szCulprit[768] = {};

    for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        google_breakpad::StackFrame* frame = stack->frames()->at(frameIndex);

        char szLine[1024];
        uint64_t moduleOffset = frame->ReturnAddress();

        if (frame->module)
        {
            const std::string modulePath = frame->module->code_file();
            const std::string moduleFile = google_breakpad::PathnameStripper::File(modulePath);
            moduleOffset -= frame->module->base_address();

            // Anything living in addons/ is a third-party plugin, so it can be symbolized locally
            // and the first such frame is the most likely culprit.
            char szSymbol[512] = {};
            if (modulePath.find("/addons/") != std::string::npos)
            {
                resolver.FillSourceLineInfo(frame, nullptr);

                if (!frame->function_name.empty())
                {
                    if (!frame->source_file_name.empty())
                        snprintf(szSymbol, sizeof(szSymbol), "%s @ %s:%d", frame->function_name.c_str(),
                                 google_breakpad::PathnameStripper::File(frame->source_file_name).c_str(), frame->source_line);
                    else
                        snprintf(szSymbol, sizeof(szSymbol), "%s", frame->function_name.c_str());
                }
                else
                    SymbolizeAddress(pszTool, modulePath.c_str(), moduleOffset, szSymbol, sizeof(szSymbol));

                if (!szCulprit[0])
                {
                    if (szSymbol[0])
                        snprintf(szCulprit, sizeof(szCulprit), "%s -> %s", moduleFile.c_str(), szSymbol);
                    else
                        snprintf(szCulprit, sizeof(szCulprit), "%s + 0x%lx", moduleFile.c_str(), moduleOffset);
                }
            }

            if (szSymbol[0])
                snprintf(szLine, sizeof(szLine), "#%zu %s + 0x%lx (%s)\n", frameIndex, moduleFile.c_str(), moduleOffset, szSymbol);
            else
                snprintf(szLine, sizeof(szLine), "#%zu %s + 0x%lx\n", frameIndex, moduleFile.c_str(), moduleOffset);
        }
        else
            snprintf(szLine, sizeof(szLine), "#%zu unknown + 0x%lx\n", frameIndex, moduleOffset);

        strncat(szReport, szLine, sizeof(szReport) - strlen(szReport) - 1);
    }

    if (szCulprit[0])
    {
        strncat(szReport, "\nSuspected culprit: ", sizeof(szReport) - strlen(szReport) - 1);
        strncat(szReport, szCulprit, sizeof(szReport) - strlen(szReport) - 1);
        strncat(szReport, "\n", sizeof(szReport) - strlen(szReport) - 1);
    }
    else
        strncat(szReport, "\n(no third-party module found in the crash stack)\n", sizeof(szReport) - strlen(szReport) - 1);

    META_LOG(&g_Plugin, "g_pExceptionHandler---- Last crash ----\n%sg_pExceptionHandler--------------------\n", szReport);

    char szTxtPath[560];
    snprintf(szTxtPath, sizeof(szTxtPath), "%s.txt", szDumpPath);
    SendDiscordReport(szReport, szTxtPath);
}

bool Plugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

    strncpy(g_szCrashGamePath, ismm->GetBaseDir(), sizeof(g_szCrashGamePath) - 1);
    ismm->Format(g_szDumpStoragePath, sizeof(g_szDumpStoragePath), "%s/addons/accelerator_local/dumps", ismm->GetBaseDir());

    struct stat st = {0};
    if (stat(g_szDumpStoragePath, &st) == -1)
    {
        if (mkdir(g_szDumpStoragePath, 0777) == -1)
        {
            ismm->Format(error, maxlen, "%s didn't exist and we couldn't create it :(", g_szDumpStoragePath);
            return false;
        }
    }
    else
        chmod(g_szDumpStoragePath, 0777);

    ismm->Format(g_szPendingCrashPath, sizeof(g_szPendingCrashPath), "%s/pending.state", g_szDumpStoragePath);
    ismm->Format(g_szSessionStatePath, sizeof(g_szSessionStatePath), "%s/session.state", g_szDumpStoragePath);

    // Load configuration file
    {
        char szConfigPath[512];
        ismm->Format(szConfigPath, sizeof(szConfigPath), "%s/addons/accelerator_local/accelerator_local.ini", ismm->GetBaseDir());

        KeyValues::AutoDelete config("accelerator_local");
        if (config->LoadFromFile(g_pFullFileSystem, szConfigPath))
            strncpy(g_szDiscordWebhook, config->GetString("discord_webhook", ""), sizeof(g_szDiscordWebhook) - 1);
        else
            META_LOG(this, "g_pExceptionHandlerFailed to load %s, Discord reporting disabled.\n", szConfigPath);
    }

    // Was the previous session healthy (reached StartupServer) before it died?
    bool bPrevSessionStarted = false;
    {
        FILE* file = fopen(g_szSessionStatePath, "r");
        if (file)
        {
            char szState[16] = {};
            fread(szState, 1, sizeof(szState) - 1, file);
            fclose(file);
            bPrevSessionStarted = strncmp(szState, "started", 7) == 0;
        }
    }
    WriteSessionState("loading");

    ProcessPendingCrash(bPrevSessionStarted);

    google_breakpad::MinidumpDescriptor descriptor(g_szDumpStoragePath);
    g_pExceptionHandler = new google_breakpad::ExceptionHandler(descriptor, NULL, dumpCallback, NULL, true, -1);

    struct sigaction oact;
    sigaction(SIGSEGV, NULL, &oact);
    SignalHandler = oact.sa_sigaction;

    {
        m_iGameFrameHookID = SH_ADD_HOOK(ISource2Server, GameFrame, g_pSource2Server, SH_MEMBER(this, &Plugin::Hook_GameFrame), true);
        m_iGameServerSteamAPIActivatedHookID = SH_ADD_HOOK(ISource2Server, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &Plugin::Hook_GameServerSteamAPIActivated), true);
        m_iGameServerSteamAPIDeactivatedHookID = SH_ADD_HOOK(ISource2Server, GameServerSteamAPIDeactivated, g_pSource2Server, SH_MEMBER(this, &Plugin::Hook_GameServerSteamAPIDeactivated), true);
        m_iStartupServerHookID = SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &Plugin::Hook_StartupServer), true);
    }

    strncpy(g_szCrashCommandLine, CommandLine()->GetCmdLine(), sizeof(g_szCrashCommandLine) - 1);

    if (late)
    {
        Hook_GameServerSteamAPIActivated();
        Hook_StartupServer({}, nullptr, g_pNetworkServerService->GetIGameServer()->GetMapName());
    }

    return true;
}

bool Plugin::Unload(char* error, size_t maxlen)
{
    SH_REMOVE_HOOK_ID(m_iGameFrameHookID);
    SH_REMOVE_HOOK_ID(m_iGameServerSteamAPIActivatedHookID);
    SH_REMOVE_HOOK_ID(m_iGameServerSteamAPIDeactivatedHookID);
    SH_REMOVE_HOOK_ID(m_iStartupServerHookID);

    delete g_pExceptionHandler;

    return true;
}

void Plugin::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
    bool bWeHaveBeenFuckedOver = false;
    struct sigaction oact;

    for (int i = 0; i < kNumHandledSignals; ++i)
    {
        sigaction(kExceptionSignals[i], NULL, &oact);

        if (oact.sa_sigaction != SignalHandler)
        {
            bWeHaveBeenFuckedOver = true;
            break;
        }
    }

    if (!bWeHaveBeenFuckedOver)
        return;

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);

    for (int i = 0; i < kNumHandledSignals; ++i)
        sigaddset(&act.sa_mask, kExceptionSignals[i]);

    act.sa_sigaction = SignalHandler;
    act.sa_flags = SA_ONSTACK | SA_SIGINFO;

    for (int i = 0; i < kNumHandledSignals; ++i)
        sigaction(kExceptionSignals[i], &act, NULL);
}

void Plugin::Hook_GameServerSteamAPIActivated()
{
    g_steamAPI.Init();
    g_pSteamHttp = g_steamAPI.SteamHTTP();

    g_httpManager.DrainQueue();
    FlushPendingDiscordReport();
}

void Plugin::Hook_GameServerSteamAPIDeactivated()
{
    g_pSteamHttp = nullptr;
}

void Plugin::Hook_StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession* pWorldSession, const char* pszMapName)
{
    strncpy(g_szCrashMap, pszMapName, sizeof(g_szCrashMap) - 1);

    WriteSessionState("started");
}

///////////////////////////////////////
const char* Plugin::GetLicense()
{
    return "GPLv3";
}

const char* Plugin::GetVersion()
{
    return VERSION_STRING;
}

const char* Plugin::GetDate()
{
    return BUILD_TIMESTAMP;
}

const char* Plugin::GetLogTag()
{
    return "AcceleratorLocal";
}

const char* Plugin::GetAuthor()
{
    return "Slynx (˙·٠● S l y n x ●٠·˙), Phoenix (˙·٠●Феникс●٠·˙), Asher Baker (asherkin)";
}

const char* Plugin::GetDescription()
{
    return "Crash Handler";
}

const char* Plugin::GetName()
{
    return "Accelerator local";
}

const char* Plugin::GetURL()
{
    return "https://github.com/FUNPLAY-pro-CS2/AcceleratorLocal";
}

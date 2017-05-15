//
// Copyright (c) 2008-2016 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Core/Platform.h"
#include "../IO/FileSystem.h"

#include <cstdio>
#include <fcntl.h>

#if !defined(__linux__) 
    #include <LibCpuId/libcpuid.h>
#endif

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <Lmcons.h> // For UNLEN. 
#else
    #include <pwd.h> 
    #include <unistd.h>
    #include <sys/sysinfo.h>
    #include <sys/utsname.h>

    #include <limits.h> // For HOST_NAME_MAX. 
#endif

#if defined(__i386__)
// From http://stereopsis.com/FPU.html
#define FPU_CW_PREC_MASK        0x0300
#define FPU_CW_PREC_SINGLE      0x0000
#define FPU_CW_PREC_DOUBLE      0x0200
#define FPU_CW_PREC_EXTENDED    0x0300
#define FPU_CW_ROUND_MASK       0x0c00
#define FPU_CW_ROUND_NEAR       0x0000
#define FPU_CW_ROUND_DOWN       0x0400
#define FPU_CW_ROUND_UP         0x0800
#define FPU_CW_ROUND_CHOP       0x0c00

inline unsigned GetFPUState()
{
    unsigned control = 0;
    __asm__ __volatile__ ("fnstcw %0" : "=m" (control));
    return control;
}

inline void SetFPUState(unsigned control)
{
    __asm__ __volatile__ ("fldcw %0" : : "m" (control));
}
#endif

#ifndef MINI_URHO
    #include <SDL/SDL.h>
#endif 

namespace Urho3D
{

#ifdef _WIN32
    static bool consoleOpened = false;
#endif

static String currentLine;
static Vector<String> arguments;

#if defined(__linux__)
struct CpuCoreCount
{
    unsigned numPhysicalCores_;
    unsigned numLogicalCores_;
};

// This function is used by all the target triplets with Linux as the OS, such as desktop Linux, etc
static void GetCPUData(struct CpuCoreCount* data)
{
    // Sanity check
    assert(data);
    // At least return 1 core
    data->numPhysicalCores_ = data->numLogicalCores_ = 1;

    FILE* fp;
    int res;
    unsigned i, j;

    fp = fopen("/sys/devices/system/cpu/present", "r");
    if (fp)
    {
        res = fscanf(fp, "%d-%d", &i, &j);
        fclose(fp);

        if (res == 2 && i == 0)
        {
            data->numPhysicalCores_ = data->numLogicalCores_ = j + 1;

            fp = fopen("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list", "r");
            if (fp)
            {
                res = fscanf(fp, "%d,%d,%d,%d", &i, &j, &i, &j);
                fclose(fp);

                // Having sibling thread(s) indicates the CPU is using HT/SMT technology
                if (res > 1)
                    data->numPhysicalCores_ /= res;
            }
        }
    }
}
#elif defined(_WIN32) 
static void GetCPUData(struct cpu_id_t* data)
{
    if (cpu_identify(0, data) < 0)
    {
        data->num_logical_cpus = 1;
        data->num_cores = 1;
    }
}
#endif

void InitFPU()
{
    // Make sure FPU is in round-to-nearest, single precision mode
    // This ensures Direct3D and OpenGL behave similarly, and all threads behave similarly
#if defined(__i386__)
    unsigned control = GetFPUState();
    control &= ~(FPU_CW_PREC_MASK | FPU_CW_ROUND_MASK);
    control |= (FPU_CW_PREC_SINGLE | FPU_CW_ROUND_NEAR);
    SetFPUState(control);
#endif
}

void ErrorDialog(const String& title, const String& message)
{
#ifndef MINI_URHO
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.CString(), message.CString(), 0);
#endif
}

void ErrorExit(const String& message, int exitCode)
{
    if (!message.Empty())
        PrintLine(message, true);

    exit(exitCode);
}

void OpenConsoleWindow()
{
#if defined(_WIN32)
    if (consoleOpened)
        return;

    AllocConsole();

    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);

    consoleOpened = true;
#endif
}

void PrintUnicode(const String& str, bool error)
{
#if defined(_WIN32)
    // If the output stream has been redirected, use fprintf instead of WriteConsoleW,
    // though it means that proper Unicode output will not work
    FILE* out = error ? stderr : stdout;
    if (!_isatty(_fileno(out)))
        fprintf(out, "%s", str.CString());
    else
    {
        HANDLE stream = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (stream == INVALID_HANDLE_VALUE)
            return;
        WString strW(str);
        DWORD charsWritten;
        WriteConsoleW(stream, strW.CString(), strW.Length(), &charsWritten, 0);
    }
#elif defined(__linux__)
    fprintf(error ? stderr : stdout, "%s", str.CString());
#endif
}

void PrintUnicodeLine(const String& str, bool error)
{
    PrintUnicode(str + "\n", error);
}

void PrintLine(const String& str, bool error)
{
    fprintf(error ? stderr : stdout, "%s\n", str.CString());
}

const Vector<String>& ParseArguments(const String& cmdLine, bool skipFirstArgument)
{
    arguments.Clear();

    unsigned cmdStart = 0, cmdEnd = 0;
    bool inCmd = false;
    bool inQuote = false;

    for (unsigned i = 0; i < cmdLine.Length(); ++i)
    {
        if (cmdLine[i] == '\"')
            inQuote = !inQuote;
        if (cmdLine[i] == ' ' && !inQuote)
        {
            if (inCmd)
            {
                inCmd = false;
                cmdEnd = i;
                // Do not store the first argument (executable name)
                if (!skipFirstArgument)
                    arguments.Push(cmdLine.Substring(cmdStart, cmdEnd - cmdStart));
                skipFirstArgument = false;
            }
        }
        else
        {
            if (!inCmd)
            {
                inCmd = true;
                cmdStart = i;
            }
        }
    }
    if (inCmd)
    {
        cmdEnd = cmdLine.Length();
        if (!skipFirstArgument)
            arguments.Push(cmdLine.Substring(cmdStart, cmdEnd - cmdStart));
    }

    // Strip double quotes from the arguments
    for (unsigned i = 0; i < arguments.Size(); ++i)
        arguments[i].Replace("\"", "");

    return arguments;
}

const Vector<String>& ParseArguments(const char* cmdLine)
{
    return ParseArguments(String(cmdLine));
}

const Vector<String>& ParseArguments(const WString& cmdLine)
{
    return ParseArguments(String(cmdLine));
}

const Vector<String>& ParseArguments(const wchar_t* cmdLine)
{
    return ParseArguments(String(cmdLine));
}

const Vector<String>& ParseArguments(int argc, char** argv)
{
    String cmdLine;

    for (int i = 0; i < argc; ++i)
        cmdLine.AppendWithFormat("\"%s\" ", (const char*)argv[i]);

    return ParseArguments(cmdLine);
}

const Vector<String>& GetArguments()
{
    return arguments;
}

String GetConsoleInput()
{
    String ret;
#if defined(_WIN32)
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE)
        return ret;

    // Use char-based input
    SetConsoleMode(input, ENABLE_PROCESSED_INPUT);

    INPUT_RECORD record;
    DWORD events = 0;
    DWORD readEvents = 0;

    if (!GetNumberOfConsoleInputEvents(input, &events))
        return ret;

    while (events--)
    {
        ReadConsoleInputW(input, &record, 1, &readEvents);
        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
        {
            unsigned c = record.Event.KeyEvent.uChar.UnicodeChar;
            if (c)
            {
                if (c == '\b')
                {
                    PrintUnicode("\b \b");
                    int length = currentLine.LengthUTF8();
                    if (length)
                        currentLine = currentLine.SubstringUTF8(0, length - 1);
                }
                else if (c == '\r')
                {
                    PrintUnicode("\n");
                    ret = currentLine;
                    currentLine.Clear();
                    return ret;
                }
                else
                {
                    // We have disabled echo, so echo manually
                    wchar_t out = c;
                    DWORD charsWritten;
                    WriteConsoleW(output, &out, 1, &charsWritten, 0);
                    currentLine.AppendUTF8(c);
                }
            }
        }
    }
#elif defined(__linux__)
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    for (;;)
    {
        int ch = fgetc(stdin);
        if (ch >= 0 && ch != '\n')
            ret += (char)ch;
        else
            break;
    }
#endif 

    return ret;
}

String GetPlatform()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return String::EMPTY;
#endif
}

unsigned GetNumPhysicalCPUs()
{
#if defined(__linux__)
    struct CpuCoreCount data;
    GetCPUData(&data);
    return data.numPhysicalCores_;
#elif defined(_WIN32) 
    struct cpu_id_t data;
    GetCPUData(&data);
    return (unsigned)data.num_cores;
#endif
}

unsigned GetNumLogicalCPUs()
{
#if defined(__linux__)
    struct CpuCoreCount data;
    GetCPUData(&data);
    return data.numLogicalCores_;
#elif defined(_WIN32) 
    struct cpu_id_t data;
    GetCPUData(&data);
    return (unsigned)data.num_logical_cpus;
#endif
}

unsigned long long GetTotalMemory()
{
#if defined(__linux__)
    struct sysinfo s;
    if(sysinfo(&s) != -1)
        return s.totalram; 
#elif defined(_WIN32)
    MEMORYSTATUSEX state; 
    state.dwLength = sizeof(state); 
    if(GlobalMemoryStatusEx(&state)) 
        return state.ullTotalPhys; 
#else 
#endif 
    return 0ull;
}

String GetLoginName() 
{
#if defined(__linux__)
    struct passwd *p = getpwuid(getuid());
    if (p) 
        return p->pw_name;
    else 
        return "(?)"; 
#elif defined(_WIN32)
    char name[UNLEN + 1];
    DWORD len = UNLEN + 1; 
    if(GetUserName(name, &len)) 
        return name; 
#else 
#endif 
    return String::EMPTY;
}

String GetHostName() 
{
#if defined(__linux__)
    char buffer[HOST_NAME_MAX + 1]; 
    if(gethostname(buffer, HOST_NAME_MAX + 1) == 0) 
        return buffer; 
#elif defined(_WIN32)
    char buffer[MAX_COMPUTERNAME_LENGTH + 1]; 
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1; 
    if(GetComputerName(buffer, &len))
        return buffer; 
#else 
#endif 
    return String::EMPTY; 
}

#if defined(_WIN32)
#define STATUS_SUCCESS (0x00000000)
typedef NTSTATUS       (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

static void GetOS(RTL_OSVERSIONINFOW *r)
{
    HMODULE m = GetModuleHandle("ntdll.dll");
    if (m)
    {
        RtlGetVersionPtr fPtr = (RtlGetVersionPtr) GetProcAddress(m, "RtlGetVersion");
        if (r && fPtr && fPtr(r) == STATUS_SUCCESS)
            r->dwOSVersionInfoSize = sizeof *r; 
    }
}
#endif 

String GetOSVersion() 
{
#if defined(__linux__)
    struct utsname u; 
    if(uname(&u) == 0)
        return String(u.sysname) + " " + u.release; 
#elif defined(_WIN32)
    RTL_OSVERSIONINFOW r;
    GetOS(&r); 
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms724832(v=vs.85).aspx
    if(r.dwMajorVersion == 5 && r.dwMinorVersion == 0) 
        return "Windows 2000"; 
    else if(r.dwMajorVersion == 5 && r.dwMinorVersion == 1) 
        return "Windows XP"; 
    else if(r.dwMajorVersion == 5 && r.dwMinorVersion == 2) 
        return "Windows XP 64-Bit Edition/Windows Server 2003/Windows Server 2003 R2"; 
    else if(r.dwMajorVersion == 6 && r.dwMinorVersion == 0) 
        return "Windows Vista/Windows Server 2008"; 
    else if(r.dwMajorVersion == 6 && r.dwMinorVersion == 1) 
        return "Windows 7/Windows Server 2008 R2"; 
    else if(r.dwMajorVersion == 6 && r.dwMinorVersion == 2) 
        return "Windows 8/Windows Server 2012";
    else if(r.dwMajorVersion == 6 && r.dwMinorVersion == 3) 
        return "Windows 8.1/Windows Server 2012 R2"; 
    else if(r.dwMajorVersion == 10 && r.dwMinorVersion == 0) 
        return "Windows 10/Windows Server 2016"; 
    else 
        return "Windows Unidentified"; 
#else 
#endif 
    return String::EMPTY; 
}

}
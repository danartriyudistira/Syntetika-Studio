/*
  SyntetikaConvert — video optimizer for Syntetika Studio
  Usage: syntetikaconvert input.mp4 [output.mp4]
  Converts video for low-latency seek: GOP=12, CRF=22, yuv420p, AAC audio
*/

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::string FindFFmpeg()
{
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir(exePath);
    size_t lastSep = exeDir.rfind('\\');
    if (lastSep != std::string::npos)
        exeDir = exeDir.substr(0, lastSep + 1);

    std::string local = exeDir + "ffmpeg.exe";
    FILE* f = fopen(local.c_str(), "r");
    if (f) { fclose(f); return local; }
#endif
    return "ffmpeg.exe";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::printf("SyntetikaConvert v1.0 — video optimizer for Syntetika Studio\n\n");
        std::printf("Usage: syntetikaconvert input.mp4 [output.mp4]\n\n");
        std::printf("Converts video: GOP=12, CRF=22, yuv420p, AAC 192k\n");
        return 0;
    }

    std::string inPath(argv[1]);
    FILE* f = fopen(inPath.c_str(), "r");
    if (!f)
    {
        std::fprintf(stderr, "ERROR: input not found: %s\n", inPath.c_str());
        return 1;
    }
    fclose(f);

    std::string outPath;
    if (argc >= 3)
        outPath = argv[2];
    else
    {
        outPath = inPath;
        size_t dot = outPath.rfind('.');
        if (dot != std::string::npos)
            outPath.insert(dot, "_opt");
        else
            outPath += "_opt.mp4";
    }

    std::string ffmpegPath = FindFFmpeg();

    std::string args = " -y -i \"" + inPath + "\""
        + " -c:v libx264 -preset fast -crf 22 -g 12 -keyint_min 12 -sc_threshold 0"
        + " -pix_fmt yuv420p -c:a aac -b:a 192k -movflags +faststart"
        + " \"" + outPath + "\"";

    std::printf("Converting: %s\n", inPath.c_str());
    std::printf("Output:    %s\n", outPath.c_str());
    std::printf("Running:   %s%s\n", ffmpegPath.c_str(), args.c_str());

#ifdef _WIN32
    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags = STARTF_USESTDHANDLES;

    std::string cmdLine = "\"" + ffmpegPath + "\"" + args;
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &si, &pi))
    {
        std::fprintf(stderr, "ERROR: failed to start ffmpeg (code %lu)\n", GetLastError());
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    int exitCode = system((ffmpegPath + args).c_str()) >> 8;
#endif

    f = fopen(outPath.c_str(), "r");
    if (f)
    {
        fclose(f);
        std::printf("\nDone: %s\n", outPath.c_str());
        return 0;
    }

    std::fprintf(stderr, "\nERROR: output file not created (exit %lu): %s\n", exitCode, outPath.c_str());
    return exitCode != 0 ? (int)exitCode : 1;
}

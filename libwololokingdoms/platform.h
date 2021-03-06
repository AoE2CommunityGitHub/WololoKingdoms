#pragma once
#include <string>

/**
 * This contains platform specific functions; things that are different between
 * Linux and Windows
 */

enum class LinkType { Soft, Dir };

/**
 * Windows
 * HACK(goto-bus-stop) Using two ifdef blocks so clang-format doesn't mess up
 * the sort…how to do this properly?
 */
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef _WIN32
#include <shellapi.h>
#include <sstream>

static void runCmd(std::wstring exe) {
  SHELLEXECUTEINFO ShExecInfo = {0};
  ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
  ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
  ShExecInfo.hwnd = nullptr;
  ShExecInfo.lpVerb = nullptr;
  ShExecInfo.lpFile = L"cmd.exe";
  ShExecInfo.lpParameters = exe.c_str();
  ShExecInfo.lpDirectory = nullptr;
  ShExecInfo.nShow = SW_SHOW;
  ShExecInfo.hInstApp = nullptr;
  ShellExecuteEx(&ShExecInfo);
  WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
}

static void mklink(LinkType type, std::string link, std::string dest) {
  std::wstring wlink;
  std::wstring wdest;
  for (auto c : link) {
    wchar_t w = c;
    wlink.push_back(w);
  }
  for (auto c : dest) {
    wchar_t w = c;
    wdest.push_back(w);
  }

  if (type == LinkType::Soft) {
    std::wstringstream line;
    line << L"/C mklink \"" << wlink << L"\" \"" << wdest << L"\"";
    runCmd(line.str().c_str());
  } else if (type == LinkType::Dir) {
#ifdef CreateSymbolicLink
    CreateSymbolicLink(wlink.c_str(), wdest.c_str(),
                       SYMBOLIC_LINK_FLAG_DIRECTORY);
#else
    std::wstringstream line;
    line << L"/C mklink /d \"" << wlink << L"\" \"" << wdest << L"\"";
    runCmd(line.str().c_str());
#endif
  }
}

#else
/**
 * Linux
 */

#include <unistd.h>

static void mklink [[maybe_unused]] ([[maybe_unused]] LinkType type,
                                     std::string link, std::string dest) {
  symlink(dest.c_str(), link.c_str());
}

#endif

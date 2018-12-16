#ifndef _WIN32
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <stdio.h>
#include "libwololokingdoms/platform.h"
#include "libwololokingdoms/string_helpers.h"
#include "paths.h"
#include <QProcess>
#include <pwd.h>
#include <iconv.h>
#include <errno.h>

namespace fs = std::filesystem;

static fs::path getHomeDir() {
  const char *homedir = getenv("HOME");
  if (homedir == NULL) {
    homedir = getpwuid(getuid())->pw_dir;
  }

  return fs::path(homedir);
}

fs::path getSteamPath() {
  return getHomeDir()/".steam"/"steam";
}

static std::string iconv_utf16_to_utf8 (std::string input) {
  const char* in_str = input.c_str();
  auto in_size = input.length();
  size_t out_size = in_size * 2;
  char* result = new char[out_size];
  char* out = result; // separate value because iconv advances the pointer

  iconv_t convert = iconv_open("UTF8", "UTF16//IGNORE");
  if (convert == (iconv_t) -1) {
    return "";
  }
  if (iconv(convert, &in_str, &in_size, &out, &out_size) == (size_t) -1) {
    return "";
  }

  iconv_close(convert);

  return result;
}

static fs::path resolveWinePath(std::string winepath) {
  QProcess process;
  process.start("winepath", QStringList() << QString::fromStdString(winepath));
  process.waitForFinished();
  QString result(process.readAllStandardOutput());
  return fs::path(result.trimmed().toStdString());
}

static std::string dumpWineRegistry(std::string regkey) {
  fs::path tempFile("wk_tmp.reg");

  QProcess wine;
  wine.start("wine", QStringList()
      << QString("regedit")
      << QString("/E")
      << QString::fromStdString(tempFile.string())
      << QString::fromStdString(regkey));
  wine.waitForFinished();
  std::ifstream stream(tempFile);
  auto result = concat_stream(stream);
  fs::remove(tempFile);
  return iconv_utf16_to_utf8(result);
}

// On linux, we can still read the Wine registry
// by first dumping the Age of Empires key to a file
fs::path getOutPath(fs::path HDPath) {
  std::stringstream registry (dumpWineRegistry("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\DirectPlay\\Applications\\Age of Empires II - The Conquerors Expansion\\"));
  std::string winepath;
  std::string prefix = "\"CurrentDirectory\"=";
  std::string line;
  while (std::getline(registry, line, '\n')) {
    if (line.substr(0, prefix.length()) == prefix) {
      winepath = line.substr(prefix.length());
      break;
    }
  }

  if (!winepath.empty()) {
    if (winepath[0] == '"')
      winepath = winepath.substr(1);
    // probably includes \r from \r\n
    if (winepath[winepath.length() - 1] == '\r')
      winepath = winepath.substr(0, winepath.length() - 1);
    if (winepath[winepath.length() - 1] == '"')
      winepath = winepath.substr(0, winepath.length() - 1);
    replace_all(winepath, "\\\\", "\\");
    return resolveWinePath(winepath);
  }

  return fs::path();
}
#endif
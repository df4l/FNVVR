#include <windows.h>

#include <string>
#include <vector>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_module_path.h"
#include "vr_options_file.h"

namespace dxvk {

  namespace {

    constexpr size_t MaxFileSize = 64 * 1024;

    std::wstring folderPath() {
      std::wstring directory = vrModuleDirectory();
      return directory.empty() ? directory : directory + L"fnvvr\\";
    }

  }


  bool VrOptionsFile::readText(const wchar_t* name, std::string& text) {
    std::wstring folder = folderPath();

    if (folder.empty())
      return false;

    std::wstring path = folder + name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE)
      return false;

    std::vector<char> buffer(MaxFileSize);
    DWORD read = 0;
    bool ok = ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr);
    CloseHandle(file);

    if (!ok) {
      Logger::warn(str::format("VR: Could not read ", str::fromws(path.c_str())));
      return false;
    }

    text.assign(buffer.data(), read);
    return true;
  }


  bool VrOptionsFile::load(VrOptions& options) {
    std::string text;

    if (!readText(L"vr_options.ini", text))
      return false;

    vrParseOptions(text, options);
    return true;
  }


  bool VrOptionsFile::save(const VrOptions& options) {
    return writeText(L"vr_options.ini", vrFormatOptions(options));
  }


  bool VrOptionsFile::writeText(const wchar_t* name, const std::string& text) {
    std::wstring folder = folderPath();

    if (folder.empty())
      return false;

    CreateDirectoryW(folder.c_str(), nullptr);

    std::wstring path = folder + name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
      nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
      Logger::warn(str::format("VR: Could not write ", str::fromws(path.c_str())));
      return false;
    }

    DWORD written = 0;
    bool ok = WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(file);

    if (!ok)
      Logger::warn(str::format("VR: Could not write ", str::fromws(path.c_str())));

    return ok;
  }

}

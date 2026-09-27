#pragma once

#include <string>

namespace zuxia {

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);

}  // namespace zuxia

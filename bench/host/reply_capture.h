#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ReplyCapture {

void install();

void clear();

void setRecording(bool recording);

[[nodiscard]] const std::vector<std::string> &lines();

[[nodiscard]] size_t lineCount();

void dispatch(const char *line);

} // namespace ReplyCapture

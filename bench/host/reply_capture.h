#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ReplyCapture {

enum class Transport { Usb, Ws };

void install();

void clear();

void setRecording(bool recording);

[[nodiscard]] const std::vector<std::string> &lines();

[[nodiscard]] size_t replyCount();

void dispatch(Transport transport, const char *line);

} // namespace ReplyCapture

#pragma once

namespace BenchArgs {

[[nodiscard]] const char *value(int argc, char **argv, const char *flag, const char *fallback);

[[nodiscard]] bool present(int argc, char **argv, const char *flag);

} // namespace BenchArgs

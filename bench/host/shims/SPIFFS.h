#pragma once

#include <cstdio>
#include <string>

class File {
  public:
    File() = default;
    explicit File(FILE *f) : m_file(f) {}

    explicit operator bool() const {
        return m_file != nullptr;
    }

    size_t size() {
        if (!m_file)
            return 0;
        const long cur = ftell(m_file);
        fseek(m_file, 0, SEEK_END);
        const long end = ftell(m_file);
        fseek(m_file, cur, SEEK_SET);
        return static_cast<size_t>(end);
    }

    size_t read(uint8_t *buf, size_t len) {
        if (!m_file)
            return 0;
        return fread(buf, 1, len, m_file);
    }

    void close() {
        if (m_file) {
            fclose(m_file);
            m_file = nullptr;
        }
    }

  private:
    FILE *m_file = nullptr;
};

class SpiffsShim {
  public:
    void setRoot(const char *root) {
        m_root = root;
    }

    File open(const char *path, const char *mode = "r") {
        std::string full = m_root;
        full += path;
        const char *fmode = (mode[0] == 'w') ? "wb" : "rb";
        return File(fopen(full.c_str(), fmode));
    }

  private:
    std::string m_root = "data";
};

inline SpiffsShim SPIFFS;

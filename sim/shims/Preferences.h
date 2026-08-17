#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

class String {
  public:
    String() = default;
    String(const char *s) : m_value(s ? s : "") {}
    String(const std::string &s) : m_value(s) {}

    const char *c_str() const {
        return m_value.c_str();
    }
    size_t length() const {
        return m_value.size();
    }
    bool isEmpty() const {
        return m_value.empty();
    }
    bool operator==(const char *other) const {
        return m_value == other;
    }

  private:
    std::string m_value;
};

class Preferences {
  public:
    bool begin(const char *ns, bool = false) {
        m_ns = ns;
        return true;
    }
    void end() {}

    String getString(const char *key, const String &fallback = String()) {
        const auto it = store().find(m_ns + "/" + key);
        return it != store().end() ? String(it->second) : fallback;
    }

    size_t putString(const char *key, const char *value) {
        store()[m_ns + "/" + key] = value;
        return strlen(value);
    }

    uint16_t getUShort(const char *key, uint16_t fallback = 0) {
        const auto it = store().find(m_ns + "/" + key);
        return it != store().end() ? static_cast<uint16_t>(atoi(it->second.c_str())) : fallback;
    }

    size_t putUShort(const char *key, uint16_t value) {
        store()[m_ns + "/" + key] = std::to_string(value);
        return 2;
    }

    uint8_t getUChar(const char *key, uint8_t fallback = 0) {
        const auto it = store().find(m_ns + "/" + key);
        return it != store().end() ? static_cast<uint8_t>(atoi(it->second.c_str())) : fallback;
    }

    size_t putUChar(const char *key, uint8_t value) {
        store()[m_ns + "/" + key] = std::to_string(value);
        return 1;
    }

    bool getBool(const char *key, bool fallback = false) {
        const auto it = store().find(m_ns + "/" + key);
        return it != store().end() ? it->second == "1" : fallback;
    }

    size_t putBool(const char *key, bool value) {
        store()[m_ns + "/" + key] = value ? "1" : "0";
        return 1;
    }

    bool remove(const char *key) {
        return store().erase(m_ns + "/" + key) > 0;
    }

    bool isKey(const char *key) {
        return store().find(m_ns + "/" + key) != store().end();
    }

    size_t putBytes(const char *key, const void *data, size_t len) {
        store()[m_ns + "/" + key] = std::string(static_cast<const char *>(data), len);
        return len;
    }

  private:
    static std::map<std::string, std::string> &store() {
        static std::map<std::string, std::string> s;
        return s;
    }

    std::string m_ns;
};

#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <type_traits>

using tjs_char = char16_t;
using tjs_int = int;
using tjs_uint32 = std::uint32_t;
#define TJS_W(value) u##value

// Small UTF-16 containers isolate the production FontSystem selection code;
// this is not a simulated TJS interpreter or a copy of its trimming logic.
class ttstr {
    std::u16string value_;
public:
    ttstr() = default;
    ttstr(const char16_t *value) : value_(value) {}
    ttstr(const char *value) { while(*value) value_ += static_cast<unsigned char>(*value++); }
    ttstr(std::u16string value) : value_(std::move(value)) {}
    ttstr(const char16_t *value, std::size_t length) : value_(value, length) {}
    const char16_t *c_str() const { return value_.c_str(); }
    int length() const { return static_cast<int>(value_.size()); }
    bool IsEmpty() const { return value_.empty(); }
    char16_t GetLastChar() const { return value_.empty() ? 0 : value_.back(); }
    int IndexOf(char16_t character) const {
        const auto found = value_.find(character);
        return found == value_.npos ? -1 : static_cast<int>(found);
    }
    int IndexOf(const char16_t *text) const {
        const auto found = value_.find(text);
        return found == value_.npos ? -1 : static_cast<int>(found);
    }
    ttstr SubString(int start, int length) const { return value_.substr(start, length < 0 ? value_.npos : length); }
    bool operator==(const ttstr &other) const { return value_ == other.value_; }
    bool operator!=(const ttstr &other) const { return !(*this == other); }
    bool operator<(const ttstr &other) const { return value_ < other.value_; }
    friend ttstr operator+(const ttstr &left, const ttstr &right) { return left.value_ + right.value_; }
};

template<class Key, class Value, class Hash>
struct tTJSHashTable {
    std::map<Key, Value> values;
    void Add(const Key &key, const Value &value) { values[key] = value; }
    Value *Find(const Key &key) {
        const auto found = values.find(key);
        return found == values.end() ? nullptr : &found->second;
    }
};

class IndividualConfigManager {
public:
    static inline bool forceDefault = false;
    static IndividualConfigManager *GetInstance() { static IndividualConfigManager instance; return &instance; }
    template<class T> T GetValue(const char *, const T &defaultValue) {
        if constexpr(std::is_same_v<T, bool>) return forceDefault;
        return defaultValue;
    }
};

#pragma once

// Solo para tests en PC. El firmware se compila contra Arduino real por separado.
#include <stdint.h>
#include <stddef.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <type_traits>

namespace fake { extern bool allowStringReserve; extern int concatBudget; }

class String {
 public:
  String() = default;
  String(const char* value) : _value(value ? value : "") {}
  String(const std::string& value) : _value(value) {}
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T value) : _value(std::to_string(value)) {}
  size_t length() const { return _value.size(); }
  bool isEmpty() const { return _value.empty(); }
  const char* c_str() const { return _value.c_str(); }
  char operator[](size_t i) const { return _value[i]; }
  bool reserve(size_t size) { if (!fake::allowStringReserve) return false; _value.reserve(size); return true; }
  bool concat(char c) { if (!canConcat()) return false; _value.push_back(c); return true; }
  bool concat(const char* text) { if (!canConcat()) return false; _value += text; return true; }
  bool concat(const String& text) { if (!canConcat()) return false; _value += text._value; return true; }
  int indexOf(char c, size_t from = 0) const {
    const size_t pos = _value.find(c, from);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  int indexOf(const char* text, size_t from = 0) const {
    const size_t pos = _value.find(text, from);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }
  String substring(size_t from, size_t to) const {
    if (from > to) std::swap(from, to);
    if (from >= _value.size()) return String();
    return _value.substr(from, std::min(to, _value.size()) - from);
  }
  String substring(size_t from) const { return substring(from, _value.size()); }
  void remove(size_t from) { if (from < _value.size()) _value.erase(from); }
  void trim() {
    const size_t first = _value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { _value.clear(); return; }
    _value = _value.substr(first, _value.find_last_not_of(" \t\r\n") - first + 1);
  }
  void toLowerCase() {
    std::transform(_value.begin(), _value.end(), _value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  }
  friend String operator+(const String& a, const String& b) { return a._value + b._value; }
  friend bool operator==(const String& a, const String& b) { return a._value == b._value; }
  friend bool operator!=(const String& a, const String& b) { return !(a == b); }
 private:
  static bool canConcat() {
    if (fake::concatBudget == 0) return false;
    if (fake::concatBudget > 0) --fake::concatBudget;
    return true;
  }
  std::string _value;
};

uint32_t millis();
void delay(uint32_t milliseconds);

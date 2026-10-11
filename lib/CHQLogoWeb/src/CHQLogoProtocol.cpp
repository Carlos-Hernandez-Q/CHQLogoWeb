#include "CHQLogoProtocol.h"

#include <string.h>

namespace chqlogo {
namespace {
struct Span {
  const char* data;
  size_t size;
};

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool equal(Span s, const char* text) {
  return s.size == strlen(text) && memcmp(s.data, text, s.size) == 0;
}
Span trim(Span s) {
  while (s.size && space(*s.data)) { ++s.data; --s.size; }
  while (s.size && space(s.data[s.size - 1])) --s.size;
  return s;
}
bool hintValid(Span s, size_t expected) {
  if (!expected || expected > 64 || s.size != expected) return false;
  for (size_t i = 0; i < s.size; ++i) {
    const char c = s.data[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') || c == '_' || c == '-')) return false;
  }
  return true;
}
bool split(Span input, Span* fields, size_t count) {
  size_t from = 0, field = 0;
  for (size_t i = 0; i <= input.size; ++i) {
    if (i == input.size || input.data[i] == ',') {
      if (field >= count) return false;
      fields[field++] = {input.data + from, i - from};
      from = i + 1;
    }
  }
  return field == count;
}

// Parser del subconjunto <rs><r .../></rs>; no acepta DTD, entidades ni
// elementos desconocidos. Valida el documento completo antes de devolver datos.
class Xml {
 public:
  Xml(const char* data, size_t length) : p(data), end(data + length) {}
  bool consume(const char* text) {
    const size_t n = strlen(text);
    if (static_cast<size_t>(end - p) < n || memcmp(p, text, n)) return false;
    p += n;
    return true;
  }
  void whitespace() { while (p != end && space(*p)) ++p; }
  bool comments() {
    whitespace();
    while (static_cast<size_t>(end - p) >= 4 && !memcmp(p, "<!--", 4)) {
      p += 4;
      while (static_cast<size_t>(end - p) >= 3 && memcmp(p, "-->", 3)) {
        if (*p == '\0' || (p[0] == '-' && p[1] == '-')) return false;
        ++p;
      }
      if (!consume("-->")) return false;
      whitespace();
    }
    return true;
  }
  bool attributes(Span& id, Span& error, Span& value, bool& empty) {
    Span names[16];
    size_t count = 0;
    id = error = value = {nullptr, 0};
    for (;;) {
      const char* before = p;
      whitespace();
      if (consume("/>")) { empty = true; return true; }
      if (consume(">")) { empty = false; return true; }
      if (p == before || count == 16) return false;
      const char* start = p;
      if (p == end || !nameStart(*p)) return false;
      ++p;
      while (p != end && nameChar(*p)) ++p;
      Span name{start, static_cast<size_t>(p - start)};
      for (size_t i = 0; i < count; ++i) {
        if (name.size == names[i].size && !memcmp(name.data, names[i].data, name.size)) return false;
      }
      names[count++] = name;
      whitespace();
      if (!consume("=")) return false;
      whitespace();
      if (p == end || (*p != '\'' && *p != '"')) return false;
      const char quote = *p++;
      start = p;
      while (p != end && *p != quote) {
        if (*p == '<' || *p == '&' || static_cast<unsigned char>(*p) < 32) return false;
        ++p;
      }
      if (p == end) return false;
      Span attr{start, static_cast<size_t>(p - start)};
      ++p;
      if (equal(name, "i")) id = attr;
      else if (equal(name, "e")) error = attr;
      else if (equal(name, "v")) value = attr;
    }
  }
  bool done() const { return p == end; }
  const char* p;
  const char* end;
 private:
  static bool nameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  }
  static bool nameChar(char c) {
    return nameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ':';
  }
};
}  // namespace

uint32_t crc32(const char* bytes, size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint8_t>(bytes[i]);
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320u : 0U);
  }
  return ~crc;
}

bool parseUInt32(const char* bytes, size_t length, uint32_t& value) {
  if (!bytes || !length || length > 10) return false;
  uint32_t parsed = 0;
  for (size_t i = 0; i < length; ++i) {
    if (bytes[i] < '0' || bytes[i] > '9') return false;
    const uint8_t digit = static_cast<uint8_t>(bytes[i] - '0');
    if (parsed > (UINT32_MAX - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
  }
  value = parsed;
  return true;
}

bool parseHex(const char* bytes, size_t length, uint16_t& value) {
  if (!bytes || !length || length > 4) return false;
  uint16_t parsed = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = bytes[i];
    unsigned digit;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else return false;
    parsed = static_cast<uint16_t>((parsed << 4) | digit);
  }
  value = parsed;
  return true;
}

bool parseStatus(const char* bytes, size_t length, uint16_t& status) {
  if (!bytes || !length) return false;
  Span s = trim({bytes, length});
  if (s.size < 4 || s.data[3] != ',') return false;
  uint32_t parsed;
  if (!parseUInt32(s.data, 3, parsed)) return false;
  status = static_cast<uint16_t>(parsed);
  return true;
}

bool parseChallenge(const char* bytes, size_t length, uint16_t successCode,
                    size_t hintLength, Challenge& challenge) {
  if (!bytes || !length) return false;
  Span fields[3];
  if (!split(trim({bytes, length}), fields, 3)) return false;
  uint32_t code, value;
  if (!parseUInt32(fields[0].data, fields[0].size, code) || code != successCode ||
      !hintValid(fields[1], hintLength) ||
      !parseUInt32(fields[2].data, fields[2].size, value)) return false;
  Challenge parsed{};
  memcpy(parsed.hint, fields[1].data, fields[1].size);
  memcpy(parsed.decimal, fields[2].data, fields[2].size);
  parsed.value = value;
  challenge = parsed;
  return true;
}

bool parseLogin(const char* bytes, size_t length, uint16_t successCode,
                size_t hintLength, char (&hint)[65]) {
  if (!bytes || !length) return false;
  Span fields[2];
  if (!split(trim({bytes, length}), fields, 2)) return false;
  uint32_t code;
  if (!parseUInt32(fields[0].data, fields[0].size, code) || code != successCode ||
      !hintValid(fields[1], hintLength)) return false;
  memcpy(hint, fields[1].data, fields[1].size);
  hint[fields[1].size] = '\0';
  return true;
}

ParseResult parseScalar(const char* bytes, size_t length, const char* resultId,
                       size_t hexDigits, bool requireValue, ScalarReply& reply) {
  if (!bytes || !resultId || !hexDigits || hexDigits > 4) return ParseResult::Malformed;
  Xml xml(bytes, length);
  xml.whitespace();
  if (xml.consume("<?xml ")) {
    while (static_cast<size_t>(xml.end - xml.p) >= 2 && memcmp(xml.p, "?>", 2)) {
      if (*xml.p == '\0' || *xml.p == '<') return ParseResult::Malformed;
      ++xml.p;
    }
    if (!xml.consume("?>")) return ParseResult::Malformed;
  }
  if (!xml.comments() || !xml.consume("<rs")) return ParseResult::Malformed;
  Span id, error, value;
  bool empty;
  if (!xml.attributes(id, error, value, empty) || empty) return ParseResult::Malformed;
  bool found = false, duplicate = false;
  ScalarReply parsed{};
  for (;;) {
    if (!xml.comments()) return ParseResult::Malformed;
    if (xml.consume("</rs>")) break;
    if (!xml.consume("<r") || !xml.attributes(id, error, value, empty)) return ParseResult::Malformed;
    uint32_t errorCode;
    if (!id.data || !id.size || !error.data ||
        !parseUInt32(error.data, error.size, errorCode) || errorCode > UINT16_MAX)
      return ParseResult::Malformed;
    if (!empty) {
      if (!xml.comments() || !xml.consume("</r>")) return ParseResult::Malformed;
    }
    if (equal(id, resultId)) {
      if (found) duplicate = true;
      found = true;
      parsed.error = static_cast<uint16_t>(errorCode);
      parsed.hasValue = value.data != nullptr;
      if (!errorCode && ((requireValue && !parsed.hasValue) ||
          (parsed.hasValue && (value.size != hexDigits ||
           !parseHex(value.data, value.size, parsed.value))))) return ParseResult::Malformed;
    }
  }
  if (!xml.comments() || !xml.done()) return ParseResult::Malformed;
  if (duplicate) return ParseResult::DuplicateResult;
  if (!found) return ParseResult::MissingResult;
  reply = parsed;
  return parsed.error ? ParseResult::Rejected : ParseResult::Ok;
}

}  // namespace chqlogo

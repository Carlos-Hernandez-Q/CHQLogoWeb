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
// SHA-256, FIPS 180-4, seccion 6.2. Sin heap ni dependencias de plataforma.
uint32_t rotateRight(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
void shaBlock(const uint8_t* block, uint32_t (&state)[8]) {
  static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  };
  uint32_t w[64];
  for (unsigned i = 0; i < 16; ++i) {
    const uint8_t* p = block + i * 4;
    w[i] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
  }
  for (unsigned i = 16; i < 64; ++i) {
    const uint32_t x = w[i - 15], y = w[i - 2];
    const uint32_t s0 = rotateRight(x, 7) ^ rotateRight(x, 18) ^ (x >> 3);
    const uint32_t s1 = rotateRight(y, 17) ^ rotateRight(y, 19) ^ (y >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (unsigned i = 0; i < 64; ++i) {
    const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
    const uint32_t t1 = h + s1 + ((e & f) ^ (~e & g)) + k[i] + w[i];
    const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
    const uint32_t t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}
}  // namespace

void sha256Hex(const char* bytes, size_t length, char (&hex)[65]) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes);
  size_t remaining = length;
  while (remaining >= 64) { shaBlock(p, state); p += 64; remaining -= 64; }
  uint8_t tail[128]{};
  if (remaining) memcpy(tail, p, remaining);
  tail[remaining] = 0x80;
  const size_t blocks = remaining < 56 ? 1 : 2;
  const uint64_t bits = static_cast<uint64_t>(length) * 8;
  for (unsigned i = 0; i < 8; ++i)
    tail[blocks * 64 - 1 - i] = static_cast<uint8_t>(bits >> (i * 8));
  shaBlock(tail, state);
  if (blocks == 2) shaBlock(tail + 64, state);
  static const char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < 32; ++i) {
    const uint8_t byte = static_cast<uint8_t>(state[i / 4] >> (24 - (i % 4) * 8));
    hex[i * 2] = digits[byte >> 4];
    hex[i * 2 + 1] = digits[byte & 15];
  }
  hex[64] = '\0';
}

bool parseSession(const char* bytes, size_t length, char (&session)[65]) {
  if (!bytes || !length) return false;
  Span text = trim({bytes, length});
  if (text.size < 3 || memcmp(text.data, "ok:", 3)) return false;
  Span token = trim({text.data + 3, text.size - 3});
  if (!hintValid(token, 64)) return false;
  memcpy(session, token.data, token.size);
  session[token.size] = '\0';
  return true;
}

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

static ParseResult parseScalarImpl(const char* bytes, size_t length, const char* resultId,
                                  size_t hexDigits, bool requireValue, ScalarReply& reply,
                                  char* discoveredId) {
  if (!bytes || (!resultId && !discoveredId) || !hexDigits || hexDigits > 4) return ParseResult::Malformed;
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
  Span selectedId{nullptr, 0};
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
    if (discoveredId || equal(id, resultId)) {
      if (discoveredId) {
        if (id.size > 32) return ParseResult::Malformed;
        for (size_t i = 0; i < id.size; ++i) {
          const char c = id.data[i];
          if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-')) return ParseResult::Malformed;
        }
        selectedId = id;
      }
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
  if (discoveredId && !parsed.error) {
    memcpy(discoveredId, selectedId.data, selectedId.size);
    discoveredId[selectedId.size] = '\0';
  }
  return parsed.error ? ParseResult::Rejected : ParseResult::Ok;
}

ParseResult parseScalar(const char* bytes, size_t length, const char* resultId,
                       size_t hexDigits, bool requireValue, ScalarReply& reply) {
  return parseScalarImpl(bytes, length, resultId, hexDigits, requireValue, reply, nullptr);
}

ParseResult discoverScalar(const char* bytes, size_t length, size_t hexDigits,
                          ScalarReply& reply, char (&resultId)[33]) {
  return parseScalarImpl(bytes, length, nullptr, hexDigits, true, reply, resultId);
}

}  // namespace chqlogo

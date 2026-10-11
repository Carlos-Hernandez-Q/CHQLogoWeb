#pragma once

#include <stddef.h>
#include <stdint.h>

// Núcleo independiente de Arduino: se ejecuta también en las pruebas nativas.
namespace chqlogo {

enum class ParseResult { Ok, Malformed, MissingResult, DuplicateResult, Rejected };

struct Challenge {
  char hint[65];
  char decimal[11];
  uint32_t value;
};

struct ScalarReply {
  uint16_t error;
  bool hasValue;
  uint16_t value;
};

uint32_t crc32(const char* bytes, size_t length);
bool parseUInt32(const char* bytes, size_t length, uint32_t& value);
bool parseHex(const char* bytes, size_t length, uint16_t& value);
bool parseStatus(const char* bytes, size_t length, uint16_t& status);
bool parseChallenge(const char* bytes, size_t length, uint16_t successCode,
                    size_t hintLength, Challenge& challenge);
bool parseLogin(const char* bytes, size_t length, uint16_t successCode,
                size_t hintLength, char (&hint)[65]);
ParseResult parseScalar(const char* bytes, size_t length, const char* resultId,
                       size_t hexDigits, bool requireValue, ScalarReply& reply);

}  // namespace chqlogo

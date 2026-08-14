// Measuring what actually got drawn.
//
// Column boxes are internal, so the tests recover them from the page: for a
// left-aligned cell the ink starts at the column's left edge, for a
// right-aligned one it ends at the right edge. Comparing those across rows is
// how "the columns stayed put" is checked without exposing internals.
//
// This lives in a header rather than the .ino because the Arduino preprocessor
// hoists generated prototypes above the sketch's own type definitions, and a
// function returning a struct declared in the .ino will not compile.
#pragma once

#include <stdint.h>

struct Ink {
  int16_t first;  ///< leftmost black pixel, or -1 if the band is blank
  int16_t last;   ///< rightmost black pixel, or -1
};

inline Ink inkSpan(const uint8_t* page, uint16_t rowBytes, uint16_t width, uint16_t y0,
                   uint16_t y1) {
  Ink s{-1, -1};
  for (uint16_t y = y0; y < y1; ++y) {
    for (uint16_t x = 0; x < width; ++x) {
      if ((page[(size_t)y * rowBytes + (x >> 3)] >> (7 - (x & 7))) & 1) {
        if (s.first < 0 || (int16_t)x < s.first) { s.first = (int16_t)x; }
        if ((int16_t)x > s.last) { s.last = (int16_t)x; }
      }
    }
  }
  return s;
}

inline uint32_t inkCount(const uint8_t* page, size_t bytes) {
  uint32_t n = 0;
  for (size_t i = 0; i < bytes; ++i) { n += (uint32_t)__builtin_popcount(page[i]); }
  return n;
}

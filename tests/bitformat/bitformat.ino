// bitformat — the output byte layout, pinned.
//
// docs/REQUIREMENTS.ja.md §10 fixes four things about the 1bpp output, and a
// driver on the far side of the library depends on all of them being exactly
// as documented: bit = 1 is black, MSB first (bit7 is the leftmost pixel),
// rows start on a byte boundary at rowBytes = (w+7)/8, and the spare bits at
// the end of a short row are 0.
//
// None of that is observable from a page that happens to look right, so this
// draws single pixels and single columns at known positions and reads the
// bytes back. Widths are chosen to sit either side of a byte boundary.

#include <LovyanGFX.hpp>
#include <LGFXVirtualCanvas.h>
#include <PaperCanvas/MonoPanel.h>

#include <string.h>

using PaperCanvas::Mono;
using PaperCanvas::MonoSink;

static const uint16_t WIDTHS[] = {1, 7, 8, 9, 63, 64, 65, 383, 384};
static constexpr uint16_t MAX_W = 384;
static constexpr uint16_t PAGE_H = 8;
static uint8_t g_buf[PaperCanvas::rowBytes(MAX_W) * PAGE_H];

static inline uint32_t gray(uint8_t v) { return lgfx::color888(v, v, v); }

// Set by the harness before each render; the draw callback is a plain function
// pointer, so this is how the case reaches it.
static uint16_t g_blackX = 0xFFFF;
static uint16_t g_pageW = 0;

static void drawNothing(LGFXVirtualCanvas& g) { g.fillScreen(gray(255)); }

static void drawAllBlack(LGFXVirtualCanvas& g) { g.fillScreen(gray(0)); }

static void drawOneColumn(LGFXVirtualCanvas& g) {
  g.fillScreen(gray(255));
  if (g_blackX < g_pageW) { g.drawFastVLine(g_blackX, 0, PAGE_H, gray(0)); }
}

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static bool render(uint16_t w, void (*draw)(LGFXVirtualCanvas&), size_t memLimit) {
  g_pageW = w;
  MonoSink sink;
  if (!sink.begin(w, PAGE_H)) { return false; }
  sink.panel().setMono(Mono::Threshold);
  sink.panel().setPageTarget(g_buf, sizeof(g_buf));
  sink.panel().beginPage();

  LGFXVirtualScreen vs(sink);
  if (memLimit) { vs.setMemoryLimit(memLimit); }
  if (!vs.begin()) { return false; }
  return vs.render(draw);
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start bitformat\n");

  bool rowBytesOk = true;
  bool whiteIsZero = true;
  bool blackIsOne = true;
  bool msbFirst = true;
  bool padZero = true;
  bool rowStride = true;

  for (size_t i = 0; i < sizeof(WIDTHS) / sizeof(WIDTHS[0]); ++i) {
    const uint16_t w = WIDTHS[i];
    const uint16_t rb = PaperCanvas::rowBytes(w);
    if (rb != (uint16_t)((w + 7) / 8)) { rowBytesOk = false; }

    // --- all white: every byte must be 0 -----------------------------------
    memset(g_buf, 0xAA, sizeof(g_buf));  // poison, so "untouched" is visible
    render(w, drawNothing, 0);
    for (uint16_t y = 0; y < PAGE_H; ++y) {
      for (uint16_t b = 0; b < rb; ++b) {
        if (g_buf[(size_t)y * rb + b] != 0x00) { whiteIsZero = false; }
      }
    }

    // --- all black: data bits 1, padding bits still 0 ----------------------
    memset(g_buf, 0xAA, sizeof(g_buf));
    render(w, drawAllBlack, 0);
    const uint16_t fullBytes = w / 8;
    const uint8_t spare = (uint8_t)(w & 7);
    for (uint16_t y = 0; y < PAGE_H; ++y) {
      const uint8_t* row = g_buf + (size_t)y * rb;
      for (uint16_t b = 0; b < fullBytes; ++b) {
        if (row[b] != 0xFF) { blackIsOne = false; }
      }
      if (spare) {
        // The high `spare` bits are pixels; the rest is padding and must be 0.
        const uint8_t want = (uint8_t)(0xFFu << (8 - spare));
        if (row[fullBytes] != want) { padZero = false; }
      }
    }

    // --- one column at a time: MSB first ------------------------------------
    // Only a few positions per width, but always the ones that straddle a byte:
    // 0, 7, 8 and the last pixel.
    const uint16_t probes[] = {0, (uint16_t)(w > 7 ? 7 : w - 1), (uint16_t)(w > 8 ? 8 : w - 1),
                               (uint16_t)(w - 1)};
    for (size_t k = 0; k < 4; ++k) {
      const uint16_t x = probes[k];
      if (x >= w) { continue; }
      g_blackX = x;
      memset(g_buf, 0xAA, sizeof(g_buf));
      render(w, drawOneColumn, 0);
      const uint16_t byteIdx = x >> 3;
      const uint8_t mask = (uint8_t)(0x80u >> (x & 7));
      for (uint16_t y = 0; y < PAGE_H; ++y) {
        const uint8_t* row = g_buf + (size_t)y * rb;
        for (uint16_t b = 0; b < rb; ++b) {
          const uint8_t want = (b == byteIdx) ? mask : 0x00;
          if (row[b] != want) {
            msbFirst = false;
            Serial.printf("#MSB w=%u x=%u y=%u byte=%u got=%02x want=%02x\n", w, x, y, b,
                          row[b], want);
            b = rb;
            y = PAGE_H;
          }
        }
      }
    }

    // --- rows do not overlap ------------------------------------------------
    // Black only on row 0 would need a per-row callback; instead check that a
    // full-black page fills exactly rb*PAGE_H bytes and leaves the poison after.
    memset(g_buf, 0xAA, sizeof(g_buf));
    render(w, drawAllBlack, 0);
    const size_t used = (size_t)rb * PAGE_H;
    for (size_t b = used; b < sizeof(g_buf); ++b) {
      if (g_buf[b] != 0xAA) { rowStride = false; break; }
    }

    Serial.printf("#WIDTH w=%u rowBytes=%u spare=%u\n", w, rb, spare);
  }

  reportCheck("rowbytes_formula", rowBytesOk, "rowBytes == (w+7)/8");
  reportCheck("white_is_zero", whiteIsZero, "white pixels are 0 bits");
  reportCheck("black_is_one", blackIsOne, "black pixels are 1 bits");
  reportCheck("msb_first", msbFirst, "bit7 is the leftmost pixel of a byte");
  reportCheck("padding_zero", padZero, "spare bits at end of row are 0");
  reportCheck("row_stride", rowStride, "page occupies exactly rowBytes*height");

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

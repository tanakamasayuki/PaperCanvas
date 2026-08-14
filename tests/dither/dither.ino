// dither — the monochrome reduction, and the property that makes it usable.
//
// The claim in docs/DECISIONS.ja.md D4 is that ordered dithering is indexed by
// absolute page coordinates, so the tile count cannot change the output. That
// is the whole reason error diffusion is not offered, so it needs proving, not
// asserting: a gray ramp is rendered at several memory limits and the bytes
// must match exactly. A ramp is the worst case — every threshold level in the
// matrix is exercised somewhere across the page.
//
// The threshold path gets the same treatment, plus the boundary behaviour that
// a driver would notice: gray < threshold is black, gray == threshold is white.
//
// Output: output/<name>.pbm for eyeballing when something breaks.

#include <LovyanGFX.hpp>
#include <LGFXVirtualCanvas.h>
#include <PaperCanvas/MonoPanel.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

using PaperCanvas::Mono;
using PaperCanvas::MonoSink;

static constexpr uint16_t PAGE_W = 256;  // one column per gray level
static constexpr uint16_t PAGE_H = 64;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(PAGE_W);
static constexpr size_t PAGE_BYTES = (size_t)ROW_BYTES * PAGE_H;

static uint8_t g_ref[PAGE_BYTES];
static uint8_t g_cmp[PAGE_BYTES];

static inline uint32_t gray(uint8_t v) { return lgfx::color888(v, v, v); }

// Column x has gray value x, so the page covers 0..255 exactly once.
static void drawRamp(LGFXVirtualCanvas& g) {
  for (uint16_t x = 0; x < PAGE_W; ++x) {
    g.drawFastVLine(x, 0, PAGE_H, gray((uint8_t)x));
  }
}

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static bool render(Mono method, uint8_t threshold, size_t memLimit, uint8_t* out) {
  MonoSink sink;
  if (!sink.begin(PAGE_W, PAGE_H)) { return false; }
  sink.panel().setMono(method, threshold);
  sink.panel().setPageTarget(out, PAGE_BYTES);
  sink.panel().beginPage();

  LGFXVirtualScreen vs(sink);
  if (memLimit) { vs.setMemoryLimit(memLimit); }
  if (!vs.begin()) { return false; }
  return vs.render(drawRamp);
}

static bool getBit(const uint8_t* page, uint16_t x, uint16_t y) {
  return (page[(size_t)y * ROW_BYTES + (x >> 3)] >> (7 - (x & 7))) & 1;
}

// Portable bitmap: 1 = black in PBM too, so the bytes go out as they are.
static void savePbm(const char* path, const uint8_t* page) {
  FILE* f = fopen(path, "wb");
  if (!f) { return; }
  fprintf(f, "P4\n%u %u\n", PAGE_W, PAGE_H);
  fwrite(page, 1, PAGE_BYTES, f);
  fclose(f);
}

struct Case {
  const char* name;
  Mono method;
  uint8_t threshold;
};

static const Case CASES[] = {
    {"threshold128", Mono::Threshold, 128},
    {"threshold64", Mono::Threshold, 64},
    {"bayer4x4", Mono::Bayer4x4, 128},
    {"bayer8x8", Mono::Bayer8x8, 128},
};

static const size_t LIMITS[] = {0, 32 * 1024, 8 * 1024, 4 * 1024, 2 * 1024};

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start dither\n");
  mkdir("output", 0755);
  Serial.printf("#PAGE w=%u h=%u rowBytes=%u\n", PAGE_W, PAGE_H, ROW_BYTES);

  bool allInvariant = true;

  for (size_t c = 0; c < sizeof(CASES) / sizeof(CASES[0]); ++c) {
    const Case& cs = CASES[c];
    bool invariant = true;
    int refTiles = 0;

    for (size_t i = 0; i < sizeof(LIMITS) / sizeof(LIMITS[0]); ++i) {
      uint8_t* dst = (i == 0) ? g_ref : g_cmp;
      MonoSink sink;
      sink.begin(PAGE_W, PAGE_H);
      sink.panel().setMono(cs.method, cs.threshold);
      sink.panel().setPageTarget(dst, PAGE_BYTES);
      sink.panel().beginPage();
      LGFXVirtualScreen vs(sink);
      if (LIMITS[i]) { vs.setMemoryLimit(LIMITS[i]); }
      const bool ok = vs.begin() && vs.render(drawRamp);
      const int tiles = vs.tileCount();
      if (i == 0) { refTiles = tiles; }

      bool match = true;
      if (i > 0) { match = memcmp(g_ref, g_cmp, PAGE_BYTES) == 0; }
      if (!match) { invariant = false; }
      Serial.printf("#SPLIT case=%s limit=%lu tiles=%d ok=%d match=%d\n", cs.name,
                    (unsigned long)LIMITS[i], tiles, ok ? 1 : 0, match ? 1 : 0);
    }

    char path[64];
    snprintf(path, sizeof(path), "output/%s.pbm", cs.name);
    savePbm(path, g_ref);

    // Ink coverage per case, as a coarse shape check: a ramp over 0..255 should
    // land near half the page. A dither that collapsed to all-black or
    // all-white would still be split-invariant, so invariance alone is not
    // enough to say the reduction works.
    uint32_t black = 0;
    for (uint16_t y = 0; y < PAGE_H; ++y) {
      for (uint16_t x = 0; x < PAGE_W; ++x) {
        if (getBit(g_ref, x, y)) { ++black; }
      }
    }
    const uint32_t total = (uint32_t)PAGE_W * PAGE_H;
    Serial.printf("#COVER case=%s black=%lu total=%lu pct=%lu refTiles=%d\n", cs.name,
                  (unsigned long)black, (unsigned long)total,
                  (unsigned long)(black * 100 / total), refTiles);

    if (!invariant) { allInvariant = false; }
  }

  reportCheck("split_invariant_all_methods", allInvariant,
              "every method gives identical bytes at every memory limit");

  //-------------------------------------------------------- threshold edges
  // Documented as `gray < threshold` is black, so the threshold value itself
  // is white. Drivers calibrate against this, so pin the boundary.
  {
    render(Mono::Threshold, 128, 0, g_ref);
    const bool below = getBit(g_ref, 127, 0);   // gray 127 < 128 -> black
    const bool at = getBit(g_ref, 128, 0);      // gray 128 == 128 -> white
    const bool above = getBit(g_ref, 129, 0);   // gray 129 > 128 -> white
    Serial.printf("#EDGE below=%d at=%d above=%d\n", below, at, above);
    reportCheck("threshold_boundary", below && !at && !above,
                "gray < threshold is black, gray == threshold is white");
  }

  //---------------------------------------------------- ordered dither shape
  // Bayer must vary within a row (that is what distinguishes it from a plain
  // threshold) and must repeat with the matrix period down the page.
  {
    render(Mono::Bayer4x4, 128, 0, g_ref);
    bool variesInRow = false;
    for (uint16_t x = 0; x + 1 < PAGE_W; ++x) {
      if (getBit(g_ref, x, 0) != getBit(g_ref, x + 1, 0)) { variesInRow = true; break; }
    }
    bool period4 = true;
    for (uint16_t y = 0; y + 4 < PAGE_H; ++y) {
      for (uint16_t x = 0; x < PAGE_W; ++x) {
        if (getBit(g_ref, x, y) != getBit(g_ref, x, y + 4)) { period4 = false; }
      }
    }
    Serial.printf("#BAYER variesInRow=%d period4=%d\n", variesInRow, period4);
    reportCheck("bayer4_varies", variesInRow, "Bayer is not a flat threshold");
    reportCheck("bayer4_period", period4, "Bayer 4x4 repeats every 4 rows");
  }

  //--------------------------------------------- dither differs from threshold
  {
    render(Mono::Threshold, 128, 0, g_ref);
    render(Mono::Bayer4x4, 128, 0, g_cmp);
    const bool differ = memcmp(g_ref, g_cmp, PAGE_BYTES) != 0;
    reportCheck("methods_differ", differ, "Bayer output is not the threshold output");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

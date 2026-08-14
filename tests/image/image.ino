// image — scaling, fitting, and the two source formats.
//
// The Fit modes are easy to implement in a way that looks right on one example
// and is wrong in general, so each is checked against a source whose aspect
// ratio does not match the box: Contain must leave a gap on one axis, Cover
// must overflow on one axis, Stretch must match both, and neither Contain nor
// Cover may distort. A square source in a square box would let all four pass.
//
// Both source formats have to produce the same picture from the same content,
// since a caller converting a logo from one to the other should not see the
// print change.
//
// Output: output/*.pbm

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ink.h"

using PaperCanvas::Fit;
using PaperCanvas::ImageOptions;
using PaperCanvas::Label;
using PaperCanvas::Receipt;
using PaperCanvas::Rect;

// Wide enough that a Fit::Cover placement is fully visible: a 40x10 source
// covering a 100x100 box comes out 400x100, and measuring it from the page
// only works if the page can hold it.
static constexpr uint16_t W = 480;
static constexpr uint16_t H = 200;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(W);
static constexpr size_t PAGE_BYTES = (size_t)ROW_BYTES * H;
static uint8_t g_page[PAGE_BYTES];
static uint8_t g_alt[PAGE_BYTES];

// A wide source: 40x10, so its aspect ratio differs from every box used below.
static constexpr uint16_t SRC_W = 40;
static constexpr uint16_t SRC_H = 10;
static uint8_t g_gray[SRC_W * SRC_H];
static uint8_t g_mono[PaperCanvas::rowBytes(SRC_W) * SRC_H];

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static void savePbm(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) { return; }
  fprintf(f, "P4\n%u %u\n", W, H);
  fwrite(g_page, 1, PAGE_BYTES, f);
  fclose(f);
}

// Solid black, so the drawn extent is exactly the placed rectangle.
static void buildSources() {
  memset(g_gray, 0, sizeof(g_gray));
  const uint16_t stride = PaperCanvas::rowBytes(SRC_W);
  memset(g_mono, 0, sizeof(g_mono));
  for (uint16_t y = 0; y < SRC_H; ++y) {
    for (uint16_t x = 0; x < SRC_W; ++x) {
      g_mono[(size_t)y * stride + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
    }
  }
}

static void inkBox(int16_t* x0, int16_t* y0, int16_t* x1, int16_t* y1) {
  *x0 = *y0 = 0x7FFF;
  *x1 = *y1 = -1;
  for (uint16_t y = 0; y < H; ++y) {
    for (uint16_t x = 0; x < W; ++x) {
      if ((g_page[(size_t)y * ROW_BYTES + (x >> 3)] >> (7 - (x & 7))) & 1) {
        if ((int16_t)x < *x0) { *x0 = (int16_t)x; }
        if ((int16_t)x > *x1) { *x1 = (int16_t)x; }
        if ((int16_t)y < *y0) { *y0 = (int16_t)y; }
        if ((int16_t)y > *y1) { *y1 = (int16_t)y; }
      }
    }
  }
}

struct FitCase {
  const char* name;
  Fit fit;
  float scale;
};

static const FitCase CASES[] = {
    {"none", Fit::None, 1.0f},
    {"scale2", Fit::Scale, 2.0f},
    {"contain", Fit::Contain, 1.0f},
    {"cover", Fit::Cover, 1.0f},
    {"stretch", Fit::Stretch, 1.0f},
};

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start image\n");
  mkdir("output", 0755);
  buildSources();
  Serial.printf("#SRC w=%u h=%u\n", SRC_W, SRC_H);

  // A box whose aspect ratio differs from the source in the other direction:
  // the source is 4:1, the box is 1:1.
  const Rect box{20, 20, 100, 100};

  for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); ++i) {
    const FitCase& c = CASES[i];
    Label lb(W, H);
    ImageOptions io;
    io.fit = c.fit;
    io.scale = c.scale;
    io.align = PaperCanvas::Align::Left;
    io.valign = PaperCanvas::VAlign::Top;
    lb.addImage(box, g_gray, SRC_W, SRC_H, io);
    lb.build(g_page, sizeof(g_page));

    char path[64];
    snprintf(path, sizeof(path), "output/fit_%s.pbm", c.name);
    savePbm(path);

    int16_t x0, y0, x1, y1;
    inkBox(&x0, &y0, &x1, &y1);
    const uint16_t dw = (uint16_t)(x1 - x0 + 1);
    const uint16_t dh = (uint16_t)(y1 - y0 + 1);
    Serial.printf("#FIT name=%s w=%u h=%u x=%d y=%d warn=0x%04x\n", c.name, dw, dh, x0, y0,
                  lb.warnings());
  }

  //-------------------------------------------------------- both source formats
  // The same picture, one as 8-bit gray and one as packed 1bpp, must print the
  // same. A caller converting a logo between the two should see no change.
  {
    Label a(W, H);
    ImageOptions io;
    io.fit = Fit::None;
    io.align = PaperCanvas::Align::Left;
    io.valign = PaperCanvas::VAlign::Top;
    a.addImage(box, g_gray, SRC_W, SRC_H, io);
    a.build(g_page, sizeof(g_page));
    memcpy(g_alt, g_page, sizeof(g_alt));

    PaperCanvas::Bitmap bmp;
    bmp.data = g_mono;
    bmp.width = SRC_W;
    bmp.height = SRC_H;
    bmp.rowBytes = PaperCanvas::rowBytes(SRC_W);
    Label b(W, H);
    b.addImage(box, bmp, io);
    b.build(g_page, sizeof(g_page));

    const bool same = memcmp(g_alt, g_page, PAGE_BYTES) == 0;
    Serial.printf("#FORMATS same=%d\n", same ? 1 : 0);
    reportCheck("formats_agree", same,
                "gray8 and 1bpp sources of the same picture print identically");
  }

  //--------------------------------------------------------------- inversion
  {
    Label lb(W, H);
    ImageOptions io;
    io.fit = Fit::None;
    io.invert = true;
    lb.addImage(box, g_gray, SRC_W, SRC_H, io);
    lb.build(g_page, sizeof(g_page));
    savePbm("output/invert.pbm");
    const uint32_t ink = inkCount(g_page, PAGE_BYTES);
    Serial.printf("#INVERT ink=%lu\n", (unsigned long)ink);
    reportCheck("invert_blanks_solid_black", ink == 0,
                "an inverted solid-black source prints as nothing");
  }

  //------------------------------------------------------ reduction warns
  {
    Label lb(W, H);
    ImageOptions io;
    io.fit = Fit::Scale;
    io.scale = 0.5f;
    lb.addImage(box, g_gray, SRC_W, SRC_H, io);
    Serial.printf("#REDUCE warn=0x%04x\n", lb.warnings());
    reportCheck("reduce_warns", (lb.warnings() & PaperCanvas::Warning_ImageScaled) != 0,
                "shrinking an image raises ImageScaled");
  }

  {
    Label lb(W, H);
    ImageOptions io;
    io.fit = Fit::Scale;
    io.scale = 2.0f;
    lb.addImage(box, g_gray, SRC_W, SRC_H, io);
    Serial.printf("#ENLARGE warn=0x%04x\n", lb.warnings());
    reportCheck("enlarge_does_not_warn",
                (lb.warnings() & PaperCanvas::Warning_ImageScaled) == 0,
                "enlarging loses nothing, so it is not reported");
  }

  //--------------------------------------------------- receipt stacks by height
  {
    Receipt r(W);
    ImageOptions io;
    io.fit = Fit::Scale;
    io.scale = 3.0f;
    const uint16_t h = r.addImage(g_gray, SRC_W, SRC_H, io);
    Serial.printf("#STACK h=%u expect=%u\n", h, (uint16_t)(SRC_H * 3));
    reportCheck("receipt_image_height", h == SRC_H * 3,
                "a stacked image takes exactly its scaled height");
  }

  //--------------------------------------------------------- split invariance
  {
    static const size_t LIMITS[] = {0, 16 * 1024, 4 * 1024, 2 * 1024};
    bool allMatch = true;
    for (size_t i = 0; i < sizeof(LIMITS) / sizeof(LIMITS[0]); ++i) {
      Label lb(W, H);
      ImageOptions io;
      io.fit = Fit::Contain;
      io.mono = PaperCanvas::Mono::Bayer4x4;
      lb.addImage(Rect{0, 0, W, H}, g_gray, SRC_W, SRC_H, io);
      lb.setMemoryLimit(LIMITS[i]);
      const bool ok = lb.build(g_page, sizeof(g_page));
      if (i == 0) {
        memcpy(g_alt, g_page, sizeof(g_alt));
      } else if (memcmp(g_alt, g_page, PAGE_BYTES) != 0) {
        allMatch = false;
      }
      Serial.printf("#SPLIT limit=%lu ok=%d\n", (unsigned long)LIMITS[i], ok ? 1 : 0);
    }
    reportCheck("split_invariant", allMatch, "images do not depend on the tile count");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

// receipt_layout — stacking, and the invariant that makes it trustworthy.
//
// A receipt resolves each element's height as it is added, so height() must be
// answerable before build() and must equal the sum of what the adds reported
// plus the margins. If those two ever disagree, the page is either clipped or
// padded and nothing downstream can tell which.
//
// The split-invariance check lives here too rather than only in monopanel/,
// because it is layout that is most likely to break it: an element that
// positions itself relative to anything tile-local would pass every test above
// and fail this one.
//
// Output: output/receipt.pbm

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

using PaperCanvas::Align;
using PaperCanvas::Fit;
using PaperCanvas::Receipt;
using PaperCanvas::TextOptions;

static constexpr uint16_t PAGE_W = 384;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(PAGE_W);
static constexpr uint16_t MAX_H = 900;
static uint8_t g_page[ROW_BYTES * MAX_H];
static uint8_t g_ref[ROW_BYTES * MAX_H];

// A small gray gradient to exercise the image path.
static constexpr uint16_t IMG_W = 64;
static constexpr uint16_t IMG_H = 32;
static uint8_t g_img[IMG_W * IMG_H];

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static void buildImage() {
  for (uint16_t y = 0; y < IMG_H; ++y) {
    for (uint16_t x = 0; x < IMG_W; ++x) {
      g_img[y * IMG_W + x] = (uint8_t)((uint32_t)(x + y) * 255u / (IMG_W + IMG_H - 2));
    }
  }
}

// Every add() return value is summed here so the total can be compared with
// height(); building the receipt and measuring it must not be two opinions.
static uint32_t g_sumHeights = 0;

static void compose(Receipt& r) {
  g_sumHeights = 0;
  r.setMargin(8, 8, 4, 4);
  r.setFont(&fonts::Font2);
  r.setAlign(Align::Center);
  g_sumHeights += r.addText("PaperCanvas Cafe");
  g_sumHeights += r.addSpace(6);

  r.setAlign(Align::Left);
  g_sumHeights += r.addRule('-');
  g_sumHeights += r.addText("Coffee");
  g_sumHeights += r.addText("Sandwich");
  g_sumHeights += r.addLine(2);

  r.setAlign(Align::Right);
  g_sumHeights += r.addText("Total 1580");

  r.setAlign(Align::Center);
  PaperCanvas::ImageOptions io;
  io.fit = Fit::Scale;
  io.scale = 2.0f;
  io.mono = PaperCanvas::Mono::Bayer4x4;
  g_sumHeights += r.addImage(g_img, IMG_W, IMG_H, io);

  // A line long enough to wrap at 376px, so wrapping is actually exercised.
  r.setAlign(Align::Left);
  g_sumHeights += r.addText(
      "Thank you for visiting us today. Please come again soon and bring a friend.");
}

static void savePbm(const char* path, const uint8_t* page, uint16_t w, uint16_t h) {
  FILE* f = fopen(path, "wb");
  if (!f) { return; }
  fprintf(f, "P4\n%u %u\n", w, h);
  fwrite(page, 1, (size_t)PaperCanvas::rowBytes(w) * h, f);
  fclose(f);
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start receipt_layout\n");
  mkdir("output", 0755);
  buildImage();

  //-------------------------------------------------- height accounting
  uint16_t pageH = 0;
  {
    Receipt r(PAGE_W);
    reportCheck("empty_height_zero", r.height() == 0, "a receipt with nothing on it is 0 high");
    compose(r);
    pageH = r.height();
    const uint32_t expect = 8 + g_sumHeights + 8;  // margins
    Serial.printf("#HEIGHT height=%u sumAdds=%lu expect=%lu count=%lu warn=0x%04x\n", pageH,
                  (unsigned long)g_sumHeights, (unsigned long)expect,
                  (unsigned long)r.count(), r.warnings());
    reportCheck("height_matches_adds", pageH == expect,
                "height() equals margins plus every add()'s reported height");
    reportCheck("height_nonzero", pageH > 0 && pageH <= MAX_H, "page height is usable");
    reportCheck("buffer_size", r.bufferSize() == (size_t)ROW_BYTES * pageH,
                "bufferSize() is rowBytes * height");

    // Wrapping happened, so the warning must say so.
    reportCheck("wrap_warned", (r.warnings() & PaperCanvas::Warning_TextWrapped) != 0,
                "wrapping raises TextWrapped");
  }

  //-------------------------------------------------- refuses a short buffer
  {
    Receipt r(PAGE_W);
    compose(r);
    const size_t need = r.bufferSize();
    memset(g_page, 0xAA, sizeof(g_page));
    const bool refused = !r.build(g_page, need - 1);
    bool untouched = true;
    for (size_t i = 0; i < need; ++i) {
      if (g_page[i] != 0xAA) { untouched = false; break; }
    }
    reportCheck("short_buffer_refused", refused, "build() fails when the buffer is too small");
    reportCheck("short_buffer_untouched", untouched,
                "a refused build() writes nothing");
  }

  //-------------------------------------------------- split invariance
  {
    static const size_t LIMITS[] = {0, 64 * 1024, 16 * 1024, 8 * 1024, 4 * 1024};
    bool allMatch = true;
    for (size_t i = 0; i < sizeof(LIMITS) / sizeof(LIMITS[0]); ++i) {
      Receipt r(PAGE_W);
      compose(r);
      r.setMemoryLimit(LIMITS[i]);
      const bool ok = r.build(g_page, sizeof(g_page));
      if (i == 0) {
        memcpy(g_ref, g_page, sizeof(g_page));
        savePbm("output/receipt.pbm", g_ref, PAGE_W, pageH);
      } else if (memcmp(g_ref, g_page, (size_t)ROW_BYTES * pageH) != 0) {
        allMatch = false;
      }
      Serial.printf("#SPLIT limit=%lu ok=%d\n", (unsigned long)LIMITS[i], ok ? 1 : 0);
    }
    reportCheck("split_invariant", allMatch, "layout does not depend on the tile count");
  }

  //-------------------------------------------------- build() == stream()
  {
    struct Ctx {
      uint8_t* dst;
      uint16_t rows;
    } ctx{g_page, 0};
    memset(g_page, 0, sizeof(g_page));

    Receipt r(PAGE_W);
    compose(r);
    r.setMemoryLimit(8 * 1024);
    const bool ok = r.stream(
        [](const PaperCanvas::Bitmap& band, uint16_t y, void* p) {
          Ctx* c = (Ctx*)p;
          memcpy(c->dst + (size_t)y * ROW_BYTES, band.data, band.rowBytes);
          ++c->rows;
        },
        &ctx);
    const bool same = memcmp(g_ref, g_page, (size_t)ROW_BYTES * pageH) == 0;
    Serial.printf("#STREAM ok=%d rows=%u pageH=%u\n", ok ? 1 : 0, ctx.rows, pageH);
    reportCheck("stream_equals_build", ok && same && ctx.rows == pageH,
                "stream() concatenates to exactly what build() produced");
  }

  //-------------------------------------------------- determinism
  {
    Receipt a(PAGE_W);
    compose(a);
    a.build(g_page, sizeof(g_page));
    bool stable = memcmp(g_ref, g_page, (size_t)ROW_BYTES * pageH) == 0;
    for (int i = 0; i < 5 && stable; ++i) {
      Receipt b(PAGE_W);
      compose(b);
      memset(g_page, 0xAA, sizeof(g_page));
      b.build(g_page, sizeof(g_page));
      if (memcmp(g_ref, g_page, (size_t)ROW_BYTES * pageH) != 0) { stable = false; }
    }
    reportCheck("deterministic", stable, "the same input always gives the same bytes");
  }

  //-------------------------------------------------- clear() resets
  {
    Receipt r(PAGE_W);
    compose(r);
    r.clear();
    reportCheck("clear_resets", r.height() == 0 && r.count() == 0,
                "clear() leaves nothing behind");
    compose(r);
    reportCheck("reuse_after_clear", r.height() == pageH,
                "the same content after clear() gives the same height");
  }

  //-------------------------------------------------- settings apply forwards
  // Documented behaviour (docs/DECISIONS.ja.md D5): changing a setting affects
  // later elements only. Two receipts that differ only in when the setting was
  // changed must therefore differ.
  {
    Receipt a(PAGE_W);
    a.setFont(&fonts::Font2);
    a.setTextSize(1);
    const uint16_t small = a.addText("Size test");
    a.setTextSize(2);
    const uint16_t big = a.addText("Size test");
    Serial.printf("#SETTING small=%u big=%u\n", small, big);
    reportCheck("setting_applies_forwards", big > small,
                "a size change affects only elements added after it");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

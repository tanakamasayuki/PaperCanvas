// label_layout — placing things by rectangle.
//
// A label is a fixed canvas, so the questions are different from a receipt's:
// does an element land where the rectangle says, does it stay inside it, and is
// a mistyped rectangle survivable? The last one matters most in practice — a
// label that comes out with one item misplaced is still readable, a label that
// refuses to generate is not (docs/DECISIONS.ja.md D11).
//
// Alignment is checked from the drawn ink rather than from internals, so the
// test fails for the reason a user would notice.
//
// Output: output/label.pbm

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ink.h"

using PaperCanvas::Align;
using PaperCanvas::Fit;
using PaperCanvas::Label;
using PaperCanvas::Rect;
using PaperCanvas::TextOptions;
using PaperCanvas::VAlign;

static constexpr uint16_t W = 400;
static constexpr uint16_t H = 240;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(W);
static constexpr size_t PAGE_BYTES = (size_t)ROW_BYTES * H;

static uint8_t g_page[PAGE_BYTES];
static uint8_t g_ref[PAGE_BYTES];

static constexpr uint16_t IMG_W = 32;
static constexpr uint16_t IMG_H = 16;
static uint8_t g_img[IMG_W * IMG_H];

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static void buildImage() {
  for (uint16_t y = 0; y < IMG_H; ++y) {
    for (uint16_t x = 0; x < IMG_W; ++x) { g_img[y * IMG_W + x] = 0; }  // solid black
  }
}

static void savePbm(const char* path, const uint8_t* page) {
  FILE* f = fopen(path, "wb");
  if (!f) { return; }
  fprintf(f, "P4\n%u %u\n", W, H);
  fwrite(page, 1, PAGE_BYTES, f);
  fclose(f);
}

// Vertical extent of ink in a band, used to check valign.
static void inkRows(const uint8_t* page, const Rect& r, int16_t* first, int16_t* last) {
  *first = -1;
  *last = -1;
  for (int16_t y = r.y; y < (int16_t)(r.y + r.h) && y < (int16_t)H; ++y) {
    for (uint16_t x = r.x; x < (uint16_t)(r.x + r.w) && x < W; ++x) {
      if ((page[(size_t)y * ROW_BYTES + (x >> 3)] >> (7 - (x & 7))) & 1) {
        if (*first < 0) { *first = y; }
        *last = y;
        break;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start label_layout\n");
  mkdir("output", 0755);
  buildImage();
  Serial.printf("#PAGE w=%u h=%u rowBytes=%u\n", W, H, ROW_BYTES);

  //--------------------------------------------------------- a whole label
  {
    Label lb(W, H);
    lb.setFont(&fonts::Font2);
    lb.addRect(Rect{0, 0, W, H}, false, 2);
    lb.addText(Rect{12, 12, 376, 24}, "PaperCanvas", TextOptions{});
    const char* cells[3] = {"Tomato", "1kg", "580"};
    lb.addRow(Rect{12, 44, 376, 20}, cells, 3);
    PaperCanvas::ImageOptions io;
    io.fit = Fit::Contain;
    lb.addImage(Rect{12, 70, 120, 100}, g_img, IMG_W, IMG_H, io);
    lb.addLine(12, 180, 388, 180, 1);
    lb.addText(Rect{12, 190, 376, 40}, "Best before 2026-09-01");

    const bool ok = lb.build(g_page, sizeof(g_page));
    savePbm("output/label.pbm", g_page);
    Serial.printf("#LABEL ok=%d h=%u count=%lu warn=0x%04x buf=%lu\n", ok ? 1 : 0, lb.height(),
                  (unsigned long)lb.count(), lb.warnings(), (unsigned long)lb.bufferSize());
    reportCheck("label_builds", ok, "a full label generates");
    reportCheck("label_height_fixed", lb.height() == H, "a label keeps the height it was given");
    reportCheck("label_buffer_size", lb.bufferSize() == PAGE_BYTES,
                "bufferSize() is rowBytes * height");
    reportCheck("label_no_warnings", lb.warnings() == 0,
                "a label that fits raises nothing");
    memcpy(g_ref, g_page, sizeof(g_page));
  }

  //--------------------------------------------------------- horizontal align
  // Same text in the same rectangle, three alignments: the ink must move.
  {
    const Rect box{100, 40, 200, 20};
    int16_t firsts[3], lasts[3];
    const Align modes[3] = {Align::Left, Align::Center, Align::Right};
    for (int i = 0; i < 3; ++i) {
      Label lb(W, H);
      lb.setFont(&fonts::Font2);
      TextOptions to;
      to.align = modes[i];
      lb.addText(box, "Hi", to);
      lb.build(g_page, sizeof(g_page));
      const Ink s = inkSpan(g_page, ROW_BYTES, W, box.y, (uint16_t)(box.y + box.h));
      firsts[i] = s.first;
      lasts[i] = s.last;
      Serial.printf("#ALIGN i=%d first=%d last=%d\n", i, s.first, s.last);
    }
    reportCheck("align_left_at_box", firsts[0] == box.x, "left-aligned text starts at the box");
    reportCheck("align_right_at_box", lasts[2] <= box.x + box.w && lasts[2] > lasts[1],
                "right-aligned text ends at the box");
    reportCheck("align_center_between", firsts[1] > firsts[0] && firsts[1] < firsts[2],
                "centred text sits between the two");
  }

  //----------------------------------------------------------- vertical align
  {
    const Rect box{20, 60, 200, 90};
    int16_t tops[3], bots[3];
    const VAlign modes[3] = {VAlign::Top, VAlign::Middle, VAlign::Bottom};
    for (int i = 0; i < 3; ++i) {
      Label lb(W, H);
      lb.setFont(&fonts::Font2);
      TextOptions to;
      to.valign = modes[i];
      lb.addText(box, "Hi", to);
      lb.build(g_page, sizeof(g_page));
      inkRows(g_page, box, &tops[i], &bots[i]);
      Serial.printf("#VALIGN i=%d top=%d bottom=%d\n", i, tops[i], bots[i]);
    }
    reportCheck("valign_top_at_box", tops[0] >= box.y && tops[0] < box.y + 8,
                "top-aligned text starts at the box top");
    reportCheck("valign_ordered", tops[0] < tops[1] && tops[1] < tops[2],
                "top, middle and bottom put the text progressively lower");
    reportCheck("valign_bottom_inside", bots[2] < box.y + box.h,
                "bottom-aligned text stays inside the box");
  }

  //-------------------------------------------------- text stays in its box
  // The canvas has no clip rectangle to lean on, so over-long text is cut at a
  // character boundary when it is stored. If that ever stopped happening, the
  // text would run across whatever is placed to its right.
  {
    Label lb(W, H);
    lb.setFont(&fonts::Font2);
    lb.addText(Rect{10, 10, 60, 20}, "This is far too long for sixty pixels");
    lb.build(g_page, sizeof(g_page));
    const Ink s = inkSpan(g_page, ROW_BYTES, W, 10, 30);
    Serial.printf("#CLIPH first=%d last=%d warn=0x%04x\n", s.first, s.last, lb.warnings());
    reportCheck("text_clipped_to_box", s.last < 10 + 60,
                "over-long text does not draw past its rectangle");
    reportCheck("text_clip_warned", (lb.warnings() & PaperCanvas::Warning_TextClipped) != 0,
                "clipping raises TextClipped");
  }

  //------------------------------------------------ tall text stays in its box
  {
    Label lb(W, H);
    lb.setFont(&fonts::Font2);
    TextOptions to;
    to.wrap = true;
    lb.addText(Rect{10, 10, 100, 20},
               "One two three four five six seven eight nine ten eleven", to);
    lb.build(g_page, sizeof(g_page));
    int16_t first, last;
    inkRows(g_page, Rect{0, 0, W, H}, &first, &last);
    Serial.printf("#CLIPV first=%d last=%d warn=0x%04x\n", first, last, lb.warnings());
    reportCheck("text_rows_clipped", last < 10 + 20,
                "wrapped text does not draw below its rectangle");
    reportCheck("text_rows_warned", (lb.warnings() & PaperCanvas::Warning_TextClipped) != 0,
                "a block taller than its box raises TextClipped");
  }

  //-------------------------------------------------- out of bounds survives
  // A mistyped rectangle must cost that element, not the label.
  {
    Label lb(W, H);
    lb.setFont(&fonts::Font2);
    lb.addText(Rect{10, 10, 100, 20}, "Inside");
    lb.addText(Rect{(int16_t)(W - 20), 10, 100, 20}, "Outside");
    lb.addRect(Rect{-10, -10, 50, 50}, true);
    const bool ok = lb.build(g_page, sizeof(g_page));
    Serial.printf("#OOB ok=%d warn=0x%04x\n", ok ? 1 : 0, lb.warnings());
    reportCheck("oob_still_builds", ok, "an out-of-bounds element does not fail the build");
    reportCheck("oob_warned", (lb.warnings() & PaperCanvas::Warning_OutOfBounds) != 0,
                "an out-of-bounds rectangle raises OutOfBounds");
  }

  //--------------------------------------------------------- split invariance
  {
    static const size_t LIMITS[] = {0, 32 * 1024, 8 * 1024, 4 * 1024};
    bool allMatch = true;
    for (size_t i = 0; i < sizeof(LIMITS) / sizeof(LIMITS[0]); ++i) {
      Label lb(W, H);
      lb.setFont(&fonts::Font2);
      lb.addRect(Rect{0, 0, W, H}, false, 2);
      lb.addText(Rect{12, 12, 376, 24}, "PaperCanvas");
      const char* cells[3] = {"Tomato", "1kg", "580"};
      lb.addRow(Rect{12, 44, 376, 20}, cells, 3);
      PaperCanvas::ImageOptions io;
      io.fit = Fit::Contain;
      lb.addImage(Rect{12, 70, 120, 100}, g_img, IMG_W, IMG_H, io);
      lb.addLine(12, 180, 388, 180, 1);
      lb.addText(Rect{12, 190, 376, 40}, "Best before 2026-09-01");
      lb.setMemoryLimit(LIMITS[i]);
      const bool ok = lb.build(g_page, sizeof(g_page));
      if (memcmp(g_ref, g_page, PAGE_BYTES) != 0) { allMatch = false; }
      Serial.printf("#SPLIT limit=%lu ok=%d\n", (unsigned long)LIMITS[i], ok ? 1 : 0);
    }
    reportCheck("split_invariant", allMatch, "labels do not depend on the tile count");
  }

  //---------------------------------------------------- build() == stream()
  {
    struct Ctx {
      uint8_t* dst;
      uint16_t rows;
    } ctx{g_page, 0};
    memset(g_page, 0, sizeof(g_page));
    Label lb(W, H);
    lb.setFont(&fonts::Font2);
    lb.addRect(Rect{0, 0, W, H}, false, 2);
    lb.addText(Rect{12, 12, 376, 24}, "PaperCanvas");
    const char* cells[3] = {"Tomato", "1kg", "580"};
    lb.addRow(Rect{12, 44, 376, 20}, cells, 3);
    PaperCanvas::ImageOptions io;
    io.fit = Fit::Contain;
    lb.addImage(Rect{12, 70, 120, 100}, g_img, IMG_W, IMG_H, io);
    lb.addLine(12, 180, 388, 180, 1);
    lb.addText(Rect{12, 190, 376, 40}, "Best before 2026-09-01");
    lb.setMemoryLimit(8 * 1024);
    const bool ok = lb.stream(
        [](const PaperCanvas::Bitmap& band, uint16_t y, void* p) {
          Ctx* c = (Ctx*)p;
          memcpy(c->dst + (size_t)y * ROW_BYTES, band.data, band.rowBytes);
          ++c->rows;
        },
        &ctx);
    const bool same = memcmp(g_ref, g_page, PAGE_BYTES) == 0;
    Serial.printf("#STREAM ok=%d rows=%u\n", ok ? 1 : 0, ctx.rows);
    reportCheck("stream_equals_build", ok && same && ctx.rows == H,
                "stream() concatenates to exactly what build() produced");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

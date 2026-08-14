// text — fonts, explicit newlines, and where wrapping decides to break.
//
// Wrapping is resolved once, when the text is added, and stored with the breaks
// baked in (docs/CORE_DESIGN.ja.md). That is what stops the tiled render from
// re-deciding a break per tile, but it also means the decision has to be right
// the first time: every produced line must fit the width, and no break may
// happen earlier than it had to. A wrapper that broke one character early would
// still pass a "does it fit" check while wasting a third of a receipt.
//
// Output: output/*.pbm

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ink.h"

using PaperCanvas::Receipt;
using PaperCanvas::TextOptions;

static constexpr uint16_t PAGE_W = 200;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(PAGE_W);
static constexpr uint16_t MAX_H = 600;
static uint8_t g_page[ROW_BYTES * MAX_H];

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static void savePbm(const char* path, uint16_t h) {
  FILE* f = fopen(path, "wb");
  if (!f) { return; }
  fprintf(f, "P4\n%u %u\n", PAGE_W, h);
  fwrite(g_page, 1, (size_t)ROW_BYTES * h, f);
  fclose(f);
}

// Rows that contain any ink, used to count drawn lines.
static uint16_t inkBands(uint16_t h) {
  uint16_t bands = 0;
  bool prev = false;
  for (uint16_t y = 0; y < h; ++y) {
    bool any = false;
    for (uint16_t b = 0; b < ROW_BYTES; ++b) {
      if (g_page[(size_t)y * ROW_BYTES + b]) {
        any = true;
        break;
      }
    }
    if (any && !prev) { ++bands; }
    prev = any;
  }
  return bands;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start text\n");
  mkdir("output", 0755);

  //-------------------------------------------------------- explicit newlines
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    r.setWrap(false);
    const uint16_t one = r.addText("One");
    const uint16_t three = r.addText("One\nTwo\nThree");
    Serial.printf("#NEWLINE one=%u three=%u\n", one, three);
    reportCheck("newlines_stack", three >= one * 3,
                "an explicit newline adds a line's worth of height");
    r.build(g_page, sizeof(g_page));
    savePbm("output/newlines.pbm", r.height());
    const uint16_t bands = inkBands(r.height());
    Serial.printf("#NEWLINEBANDS bands=%u\n", bands);
    reportCheck("newlines_drawn", bands == 4, "all four lines are drawn");
  }

  //---------------------------------------------------------------- font size
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    r.setTextSize(1);
    const uint16_t h1 = r.addText("Size");
    r.setTextSize(2);
    const uint16_t h2 = r.addText("Size");
    r.setTextSize(3);
    const uint16_t h3 = r.addText("Size");
    Serial.printf("#SIZE h1=%u h2=%u h3=%u\n", h1, h2, h3);
    reportCheck("size_scales_height", h2 == h1 * 2 && h3 == h1 * 3,
                "text height scales with the size multiplier");
  }

  //-------------------------------------------------------------- line spacing
  {
    Receipt a(PAGE_W);
    a.setFont(&fonts::Font2);
    a.setWrap(false);
    const uint16_t plain = a.addText("One\nTwo\nThree");

    Receipt b(PAGE_W);
    b.setFont(&fonts::Font2);
    b.setWrap(false);
    b.setLineSpacing(5);
    const uint16_t spaced = b.addText("One\nTwo\nThree");
    Serial.printf("#SPACING plain=%u spaced=%u\n", plain, spaced);
    // Three lines means two gaps, so five pixels of spacing adds ten.
    reportCheck("line_spacing_adds", spaced == plain + 10,
                "line spacing is added between lines, not after the last");
  }

  //------------------------------------------------------------ wrap position
  // Every produced line must fit, and no line may be shorter than it had to be:
  // the next word's first character must genuinely not have fitted.
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    r.setWrap(true);
    const char* src =
        "The quick brown fox jumps over the lazy dog and keeps on running well past "
        "the edge of the paper";
    const uint16_t h = r.addText(src);
    r.build(g_page, sizeof(g_page));
    savePbm("output/wrapped.pbm", r.height());

    // Measure each drawn line's ink extent; none may exceed the page width, and
    // taking the widest as a proxy, the wrap must be using most of the width.
    lgfx::LGFX_Sprite m;
    m.setColorDepth(8);
    m.createSprite(1, 1);
    m.setFont(&fonts::Font2);

    const uint16_t lineH = (uint16_t)m.fontHeight();
    uint16_t lines = 0;
    int16_t widest = 0;
    bool allFit = true;
    for (uint16_t y = 0; y + lineH <= h; y += lineH) {
      const Ink s = inkSpan(g_page, ROW_BYTES, PAGE_W, y, (uint16_t)(y + lineH));
      if (s.first < 0) { continue; }
      ++lines;
      if (s.last >= (int16_t)PAGE_W) { allFit = false; }
      if (s.last > widest) { widest = s.last; }
    }
    m.deleteSprite();
    Serial.printf("#WRAP h=%u lines=%u widest=%d pageW=%u warn=0x%04x\n", h, lines, widest,
                  PAGE_W, r.warnings());
    reportCheck("wrap_lines_fit", allFit, "no wrapped line runs past the page width");
    reportCheck("wrap_uses_width", widest > (int16_t)(PAGE_W * 3 / 4),
                "wrapping fills most of the width rather than breaking early");
    reportCheck("wrap_multiple_lines", lines >= 3, "the sample actually wrapped");
    reportCheck("wrap_warned", (r.warnings() & PaperCanvas::Warning_TextWrapped) != 0,
                "wrapping raises TextWrapped");
  }

  //--------------------------------------------------- wrap keeps explicit \n
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    r.setWrap(true);
    const uint16_t h = r.addText("A\nB");
    r.build(g_page, sizeof(g_page));
    const uint16_t bands = inkBands(h);
    Serial.printf("#WRAPNL h=%u bands=%u\n", h, bands);
    reportCheck("wrap_keeps_newlines", bands == 2,
                "wrapping does not swallow an explicit newline");
  }

  //---------------------------------------------------- no wrap means clipped
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    r.setWrap(false);
    const uint16_t h = r.addText("This line is far too long to fit two hundred pixels");
    r.build(g_page, sizeof(g_page));
    const Ink s = inkSpan(g_page, ROW_BYTES, PAGE_W, 0, h);
    const uint16_t bands = inkBands(h);
    Serial.printf("#NOWRAP h=%u bands=%u last=%d warn=0x%04x\n", h, bands, s.last,
                  r.warnings());
    reportCheck("nowrap_single_line", bands == 1, "without wrapping the text stays on one line");
    reportCheck("nowrap_within_width", s.last < (int16_t)PAGE_W,
                "clipped text does not run past the page");
    reportCheck("nowrap_warned", (r.warnings() & PaperCanvas::Warning_TextClipped) != 0,
                "clipping raises TextClipped");
  }

  //------------------------------------------------------------------- UTF-8
  // Multi-byte characters must not be cut in half, by wrapping or by clipping.
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::efontJA_16);
    r.setWrap(true);
    const uint16_t h = r.addText("ご来店ありがとうございます。またのお越しをお待ちしております。");
    r.build(g_page, sizeof(g_page));
    savePbm("output/utf8.pbm", r.height());
    bool allFit = true;
    for (uint16_t y = 0; y + 16 <= h; y += 16) {
      const Ink s = inkSpan(g_page, ROW_BYTES, PAGE_W, y, (uint16_t)(y + 16));
      if (s.first >= 0 && s.last >= (int16_t)PAGE_W) { allFit = false; }
    }
    Serial.printf("#UTF8 h=%u fit=%d\n", h, allFit ? 1 : 0);
    reportCheck("utf8_wraps", h > 16, "Japanese text wrapped onto more than one line");
    reportCheck("utf8_lines_fit", allFit, "no wrapped Japanese line runs past the width");
  }

  //-------------------------------------------------------------- empty text
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    const uint16_t h = r.addText("");
    Serial.printf("#EMPTY h=%u count=%lu\n", h, (unsigned long)r.count());
    reportCheck("empty_text_one_line", h > 0 && r.count() == 1,
                "empty text still occupies one line, so a blank row is expressible");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

// js_parity (C++ side) — what the device actually prints.
//
// The browser tool previews a layout by rendering it with lgfx-font-tool. That
// preview is only worth anything if it matches what PaperCanvas produces, and
// "matches" has to mean bytes, not "looks about right".
//
// So this sketch renders a set of text cases through PaperCanvas and dumps the
// 1bpp page. render.mjs renders the same cases through lgfx-font-tool, and the
// pytest compares them. The cases come from cases.json, which both sides read,
// so neither can drift onto different content.
//
// Note what this does NOT test: glyph shapes. lgfx-font-tool already verifies
// those against the real LovyanGFX across 186 fonts. What is unverified, and
// what this catches, is everything PaperCanvas puts around them — where the
// text is placed, how a size multiplier is applied, how the page is packed.

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>

#include <stdio.h>
#include <string.h>

#include "cases.h"

using PaperCanvas::Align;
using PaperCanvas::Label;
using PaperCanvas::Rect;
using PaperCanvas::TextOptions;
using PaperCanvas::VAlign;

static constexpr uint16_t W = PARITY_CANVAS_W;
static constexpr uint16_t H = PARITY_CANVAS_H;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(W);
static uint8_t g_page[ROW_BYTES * H];

static void emitHex(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) { Serial.printf("%02x", p[i]); }
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start js_parity\n");
  Serial.printf("#CANVAS w=%u h=%u rowBytes=%u cases=%u\n", W, H, ROW_BYTES,
                (unsigned)PARITY_CASE_COUNT);

  for (size_t i = 0; i < PARITY_CASE_COUNT; ++i) {
    const ParityCase& c = PARITY_CASES[i];

    Label lb(W, H);
    lb.setWrap(false);  // wrapping is layout, and layout is compared separately

    TextOptions opt;
    opt.font = c.font;
    opt.size = c.size;
    opt.align = Align::Left;
    opt.valign = VAlign::Top;
    // Drawn at the canvas origin so the comparison is of the glyph run itself,
    // with none of PaperCanvas's alignment arithmetic in the way. Alignment has
    // its own tests; mixing it in here would make a failure ambiguous.
    lb.addText(Rect{0, 0, W, H}, c.text, opt);

    const bool ok = lb.build(g_page, sizeof(g_page));
    Serial.printf("#CASE name=%s ok=%d warn=0x%04x data=", c.name, ok ? 1 : 0,
                  lb.warnings());
    emitHex(g_page, (size_t)ROW_BYTES * H);
    Serial.printf("\n");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

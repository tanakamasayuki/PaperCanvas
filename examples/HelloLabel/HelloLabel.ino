// HelloLabel — a fixed-size label with everything placed by rectangle.
//
// The difference from a receipt is not cosmetic: a receipt grows downwards and
// a label cannot. Nothing here can push anything else out of place, which is
// why every element takes a rectangle and why text that does not fit is cut
// rather than wrapped by default.
//
// The rectangles are the tedious part, which is what the browser tool exists
// for. Writing a few by hand first is still worth it — it makes the coordinate
// system obvious.

#include <M5Unified.h>
#include <PaperCanvas.h>

using PaperCanvas::Align;
using PaperCanvas::Bitmap;
using PaperCanvas::Fit;
using PaperCanvas::Label;
using PaperCanvas::Rect;
using PaperCanvas::TextOptions;
using PaperCanvas::VAlign;

// A 50 x 30 mm label at 203 dpi. mmToPx is constexpr, so this is worked out at
// compile time; the API itself is pixels only, deliberately (see docs).
static constexpr uint16_t DPI = 203;
static constexpr uint16_t W = PaperCanvas::mmToPx(50.0f, DPI);  // 400
static constexpr uint16_t H = PaperCanvas::mmToPx(30.0f, DPI);  // 240

static uint8_t page[PaperCanvas::rowBytes(W) * H];

// A small logo: 8-bit gray, one byte per pixel, 0 = black. A 1bpp Bitmap works
// just as well; use whichever your asset pipeline produces.
static constexpr uint16_t LOGO_W = 48;
static constexpr uint16_t LOGO_H = 32;
static uint8_t logo[LOGO_W * LOGO_H];

static void buildLogo() {
  for (uint16_t y = 0; y < LOGO_H; ++y) {
    for (uint16_t x = 0; x < LOGO_W; ++x) {
      const bool ring = (x - LOGO_W / 2) * (x - LOGO_W / 2) +
                            (y - LOGO_H / 2) * (y - LOGO_H / 2) <
                        (LOGO_H / 2) * (LOGO_H / 2);
      logo[y * LOGO_W + x] = ring ? 0 : 255;
    }
  }
}

static void showBitmap(const Bitmap& bmp, int32_t x, int32_t y) {
  M5.Display.fillRect(x, y, bmp.width, bmp.height, TFT_WHITE);
  for (uint16_t row = 0; row < bmp.height; ++row) {
    const uint8_t* line = bmp.data + (size_t)row * bmp.rowBytes;
    for (uint16_t col = 0; col < bmp.width; ++col) {
      if ((line[col >> 3] >> (7 - (col & 7))) & 1) {
        M5.Display.drawPixel(x + col, y + row, TFT_BLACK);
      }
    }
  }
}

void setup() {
  M5.begin();
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_DARKGREY);
  buildLogo();

  Label label(W, H);
  label.setFont(&fonts::efontJA_16);

  // A border, so the label edge is visible on the preview.
  label.addRect(Rect{0, 0, W, H}, false, 2);

  TextOptions title;
  title.align = Align::Center;
  label.addText(Rect{8, 8, W - 16, 20}, "産地直送", title);

  // Three columns in one row. They line up across rows because the widths come
  // from the layout, not from this row's text.
  label.addRow(Rect{8, 34, W - 16, 20}, "トマト", "1kg", "580");

  label.addImage(Rect{8, 60, LOGO_W + 16, 100}, logo, LOGO_W, LOGO_H,
                 {.align = Align::Center, .valign = VAlign::Middle, .fit = Fit::Contain});

  label.addLine(8, 170, W - 8, 170, 1);

  // Two lines via an explicit newline. Wrapping is off by default on a label,
  // because a fixed rectangle has nowhere to grow into.
  TextOptions small;
  small.size = 0.75f;
  small.valign = VAlign::Top;
  label.addText(Rect{8, 178, W - 16, 54},
                "賞味期限 2026-09-01\n要冷蔵 10℃以下", small);

  if (!label.build(page, sizeof(page))) {
    M5.Display.setTextColor(TFT_RED);
    M5.Display.drawString("build failed", 4, 4);
    return;
  }

  // OutOfBounds means a rectangle stuck out past the canvas, TextClipped that
  // something did not fit its box. Neither stops the label from being made.
  if (label.warnings()) {
    Serial.printf("warnings: 0x%04x\n", label.warnings());
  }

  Bitmap bmp;
  bmp.data = page;
  bmp.width = W;
  bmp.height = H;
  bmp.rowBytes = PaperCanvas::rowBytes(W);
  showBitmap(bmp, 0, 0);
}

void loop() { M5.delay(100); }

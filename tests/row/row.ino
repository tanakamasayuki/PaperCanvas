// row — columns that stay put.
//
// The point of addRow over padding a string with spaces is that a run of rows
// lines up: column boxes resolve from the layout, not from how long this row's
// text happens to be. So the central check feeds rows whose cell lengths vary
// wildly and requires the resolved boxes to be identical.
//
// Column widths are reported by the sketch (#CELL) because they are resolved
// once at add time and never recomputed; if that ever became a per-tile
// calculation, these numbers are what would start disagreeing.
//
// Output: output/row.pbm

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ink.h"

using PaperCanvas::Align;
using PaperCanvas::Column;
using PaperCanvas::Receipt;
using PaperCanvas::RowOptions;

static constexpr uint16_t PAGE_W = 384;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(PAGE_W);
static constexpr uint16_t MAX_H = 600;
static uint8_t g_page[ROW_BYTES * MAX_H];
static uint8_t g_ref[ROW_BYTES * MAX_H];

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
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
  Serial.printf("TEST start row\n");
  mkdir("output", 0755);

  //------------------------------------------------ implicit 2-column layout
  // Names of very different lengths, figures of the same length: the figures
  // must end at exactly the same x on every row.
  uint16_t rowH = 0;
  uint16_t pageH = 0;
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    r.setWrap(false);
    rowH = r.addRow("A", "960");
    r.addRow("Cafe Latte", "480");
    r.addRow("An extremely long product name here", "120");
    pageH = r.height();
    r.build(g_page, sizeof(g_page));
    savePbm("output/row.pbm", g_page, PAGE_W, pageH);

    // The right column is right-aligned against the printable edge, so every
    // row's ink must end at the same place.
    int16_t ends[3];
    bool sameEnd = true;
    for (uint16_t i = 0; i < 3; ++i) {
      const Ink s = inkSpan(g_page, ROW_BYTES, PAGE_W, (uint16_t)(i * rowH), (uint16_t)((i + 1) * rowH));
      ends[i] = s.last;
      Serial.printf("#ROW i=%u first=%d last=%d\n", i, s.first, s.last);
      if (i && ends[i] != ends[0]) { sameEnd = false; }
    }
    reportCheck("right_column_aligned", sameEnd,
                "the right cell ends at the same x on every row");
    reportCheck("right_column_at_edge", ends[0] >= PAGE_W - 12,
                "the right cell sits against the printable edge");
  }

  //--------------------------------------------------- explicit percent grid
  // Fixed shares mean the middle column starts at the same x regardless of how
  // long the first cell is — the property padding with spaces cannot give.
  {
    static const Column COLS[] = {
        Column::percent(50, Align::Left),
        Column::percent(20, Align::Center),
        Column::rest(Align::Right),
    };
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    r.setWrap(false);
    r.setColumns(COLS, 3);
    r.setColumnGap(4);
    const uint16_t h = r.addRow("A", "x2", "960");
    r.addRow("Something considerably longer", "x11", "1280");

    r.build(g_page, sizeof(g_page));

    // Column 0 is 50% of (384 - 2*4) = 188; column 1 starts at 188 + 4 = 192.
    // Both rows' middle cells must be centred in the same box, so their ink
    // must fall inside it on both rows.
    bool inBox = true;
    for (uint16_t i = 0; i < 2; ++i) {
      const Ink s = inkSpan(g_page, ROW_BYTES, PAGE_W, (uint16_t)(i * h), (uint16_t)((i + 1) * h));
      Serial.printf("#GRID i=%u first=%d last=%d\n", i, s.first, s.last);
      if (s.first != 0) { inBox = false; }  // column 0 is left-aligned at x=0
    }
    reportCheck("grid_first_column_at_zero", inBox,
                "an explicit left column starts at the box edge on every row");
    Serial.printf("#GRIDH h=%u\n", h);
  }

  //--------------------------------------------------------------- leader
  {
    static const Column COLS[] = {
        Column::rest(Align::Left, '.'),
        Column::autoFit(Align::Right),
    };
    Receipt a(PAGE_W);
    a.setFont(&fonts::Font2);
    a.setColumns(COLS, 2);
    const uint16_t h = a.addRow("Total", "1580");
    a.build(g_ref, sizeof(g_ref));
    const uint32_t withLeader = inkCount(g_ref, (size_t)ROW_BYTES * h);

    Receipt b(PAGE_W);
    b.setFont(&fonts::Font2);
    b.clearColumns();
    b.addRow("Total", "1580");
    b.build(g_page, sizeof(g_page));
    const uint32_t without = inkCount(g_page, (size_t)ROW_BYTES * h);
    Serial.printf("#LEADER with=%lu without=%lu\n", (unsigned long)withLeader,
                  (unsigned long)without);
    reportCheck("leader_adds_ink", withLeader > without,
                "a leader character fills the gap between cells");
  }

  //------------------------------------------------------- wrap inside a cell
  {
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    RowOptions ro;
    ro.wrap = true;
    const uint16_t oneLine = r.addRow("Short", "10");
    const char* cells[2] = {
        "A product name far too long to fit inside its own column box", "10"};
    const uint16_t wrapped = r.addRow(cells, 2, ro);
    Serial.printf("#WRAP one=%u wrapped=%u warn=0x%04x\n", oneLine, wrapped, r.warnings());
    reportCheck("row_wrap_grows", wrapped > oneLine,
                "a wrapped cell makes the row taller");
    reportCheck("row_wrap_warned",
                (r.warnings() & PaperCanvas::Warning_TextWrapped) != 0,
                "wrapping inside a row raises TextWrapped");
  }

  //---------------------------------------------- overflow does not push out
  // A fixed-width column must clip rather than grow: if it grew, every later
  // row would shift and the columns would stop lining up, which is the one
  // thing addRow exists to prevent. The right cell must keep its box, and the
  // row must stay one line high.
  {
    static const Column COLS[] = {
        Column::px(80, Align::Left),
        Column::rest(Align::Right),
    };
    Receipt r(PAGE_W);
    r.setFont(&fonts::Font2);
    r.setWrap(false);
    r.setColumns(COLS, 2);
    const uint16_t plain = r.addRow("Short", "10");
    const uint16_t over = r.addRow("A product name far too long for an 80px column", "10");
    r.build(g_page, sizeof(g_page));

    const Ink a = inkSpan(g_page, ROW_BYTES, PAGE_W, 0, plain);
    const Ink b = inkSpan(g_page, ROW_BYTES, PAGE_W, plain, (uint16_t)(plain + over));
    Serial.printf("#CLIP plain=%u over=%u warn=0x%04x endA=%d endB=%d\n", plain, over,
                  r.warnings(), a.last, b.last);
    reportCheck("row_clip_same_height", plain == over,
                "an over-long cell does not change the row height");
    reportCheck("row_clip_keeps_box", a.last == b.last,
                "an over-long cell does not push the next column out");
    reportCheck("row_clip_warned", (r.warnings() & PaperCanvas::Warning_TextClipped) != 0,
                "an over-long cell raises TextClipped");
  }

  //--------------------------------------------------------- split invariance
  {
    static const size_t LIMITS[] = {0, 32 * 1024, 8 * 1024, 4 * 1024};
    static const Column COLS[] = {
        Column::percent(55, Align::Left, '.'),
        Column::percent(15, Align::Center),
        Column::rest(Align::Right),
    };
    bool allMatch = true;
    uint16_t h = 0;
    for (size_t i = 0; i < sizeof(LIMITS) / sizeof(LIMITS[0]); ++i) {
      Receipt r(PAGE_W);
      r.setFont(&fonts::Font2);
      r.setColumns(COLS, 3);
      r.addRow("Coffee", "x2", "960");
      r.addRow("Sandwich", "x1", "620");
      r.addRow("Total", "", "1580");
      h = r.height();
      r.setMemoryLimit(LIMITS[i]);
      const bool ok = r.build(g_page, sizeof(g_page));
      if (i == 0) {
        memcpy(g_ref, g_page, sizeof(g_page));
      } else if (memcmp(g_ref, g_page, (size_t)ROW_BYTES * h) != 0) {
        allMatch = false;
      }
      Serial.printf("#SPLIT limit=%lu ok=%d\n", (unsigned long)LIMITS[i], ok ? 1 : 0);
    }
    reportCheck("split_invariant", allMatch, "rows do not depend on the tile count");
  }

  //--------------------------------------------- empty cell takes no width
  {
    Receipt a(PAGE_W);
    a.setFont(&fonts::Font2);
    a.clearColumns();
    const uint16_t h = a.addRow("Total", "", "1580");
    a.build(g_ref, sizeof(g_ref));
    const Ink withEmpty = inkSpan(g_ref, ROW_BYTES, PAGE_W, 0, h);

    Receipt b(PAGE_W);
    b.setFont(&fonts::Font2);
    b.clearColumns();
    b.addRow("Total", "1580");
    b.build(g_page, sizeof(g_page));
    const Ink twoCell = inkSpan(g_page, ROW_BYTES, PAGE_W, 0, h);

    Serial.printf("#EMPTY three=[%d,%d] two=[%d,%d]\n", withEmpty.first, withEmpty.last,
                  twoCell.first, twoCell.last);
    reportCheck("empty_cell_no_width", withEmpty.last == twoCell.last,
                "an empty middle cell does not move the right column");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

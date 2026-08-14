// failure — what happens when generation cannot succeed.
//
// Two rules, and they pull in opposite directions, which is why both are here:
//
//   Warnings never stop a build. A mistyped rectangle costs that element.
//   Real failures never fall back. A build that cannot produce the requested
//     page returns false and writes nothing, rather than quietly producing a
//     smaller or partial one (docs/DECISIONS.ja.md D11, and the same policy
//     LGFXVirtualCanvas takes for allocation failure).
//
// The second rule is the one that is easy to erode: it is tempting to shrink
// the page or emit what fits. A caller that got `true` back and a short page has
// no way to tell, and prints it.
//
// The buffer is poisoned before every failing call so "wrote nothing" is
// checked rather than assumed.

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>

#include <stdio.h>
#include <string.h>

using PaperCanvas::Label;
using PaperCanvas::Receipt;
using PaperCanvas::Rect;

static constexpr uint16_t W = 200;
static constexpr uint16_t MAX_H = 200;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(W);
static uint8_t g_page[ROW_BYTES * MAX_H];

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static void poison() { memset(g_page, 0xA5, sizeof(g_page)); }

static bool stillPoisoned(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (g_page[i] != 0xA5) { return false; }
  }
  return true;
}

static void buildReceipt(Receipt& r) {
  r.setFont(&fonts::Font2);
  r.setWrap(false);
  r.addText("One");
  r.addText("Two");
  r.addLine(2);
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start failure\n");

  //---------------------------------------------------- buffer one byte short
  {
    Receipt r(W);
    buildReceipt(r);
    const size_t need = r.bufferSize();
    poison();
    const bool ok = r.build(g_page, need - 1);
    const bool untouched = stillPoisoned(need);
    Serial.printf("#SHORT need=%lu ok=%d untouched=%d\n", (unsigned long)need, ok ? 1 : 0,
                  untouched ? 1 : 0);
    reportCheck("short_buffer_refused", !ok, "one byte short is refused");
    reportCheck("short_buffer_untouched", untouched, "a refused build writes nothing");
  }

  //-------------------------------------------------------- exact buffer works
  // The boundary in the other direction: exactly bufferSize() must succeed, or
  // the documented size is wrong.
  {
    Receipt r(W);
    buildReceipt(r);
    const size_t need = r.bufferSize();
    poison();
    const bool ok = r.build(g_page, need);
    Serial.printf("#EXACT need=%lu ok=%d\n", (unsigned long)need, ok ? 1 : 0);
    reportCheck("exact_buffer_accepted", ok, "exactly bufferSize() is enough");
  }

  //--------------------------------------------------------------- null buffer
  {
    Receipt r(W);
    buildReceipt(r);
    const bool ok = r.build(nullptr, 1000);
    reportCheck("null_buffer_refused", !ok, "a null buffer is refused");
  }

  //--------------------------------------------------------------- empty page
  // A receipt with nothing on it has no height, so there is no page to make.
  {
    Receipt r(W);
    poison();
    const bool ok = r.build(g_page, sizeof(g_page));
    Serial.printf("#EMPTY h=%u ok=%d size=%lu\n", r.height(), ok ? 1 : 0,
                  (unsigned long)r.bufferSize());
    reportCheck("empty_receipt_refused", !ok, "a receipt with no content does not build");
    reportCheck("empty_receipt_untouched", stillPoisoned(sizeof(g_page)),
                "a refused empty build writes nothing");
  }

  //----------------------------------------------------------- zero dimensions
  {
    Label lb(0, 100);
    poison();
    const bool ok = lb.build(g_page, sizeof(g_page));
    reportCheck("zero_width_refused", !ok, "a zero-width label does not build");
  }
  {
    Label lb(100, 0);
    poison();
    const bool ok = lb.build(g_page, sizeof(g_page));
    reportCheck("zero_height_refused", !ok, "a zero-height label does not build");
  }

  //-------------------------------------------------------------- null stream
  {
    Receipt r(W);
    buildReceipt(r);
    const bool ok = r.stream(nullptr, nullptr);
    reportCheck("null_stream_refused", !ok, "stream() without a callback is refused");
  }

  //--------------------------------------------- a refusal leaves state intact
  // After a failed build the page must be unchanged, so the caller can fix the
  // buffer and try again rather than rebuilding the document.
  {
    Receipt r(W);
    buildReceipt(r);
    const uint16_t h1 = r.height();
    const size_t n1 = r.count();
    r.build(g_page, 1);  // refused
    const uint16_t h2 = r.height();
    const size_t n2 = r.count();
    poison();
    const bool ok = r.build(g_page, sizeof(g_page));
    Serial.printf("#RETRY h1=%u h2=%u n1=%lu n2=%lu retryOk=%d\n", h1, h2,
                  (unsigned long)n1, (unsigned long)n2, ok ? 1 : 0);
    reportCheck("refusal_keeps_state", h1 == h2 && n1 == n2,
                "a refused build does not disturb the document");
    reportCheck("retry_after_refusal", ok, "the same page builds once given room");
  }

  //-------------------------------------------------- no fallback to a smaller page
  // The heart of the policy. A memory limit too small to hold even one tile row
  // must fail outright, not silently produce a shorter page or a blank one that
  // looks like a successful print.
  {
    Receipt r(W);
    buildReceipt(r);
    const size_t need = r.bufferSize();
    poison();
    r.setMemoryLimit(1);  // cannot hold a single row of the tile sprite
    const bool ok = r.build(g_page, sizeof(g_page));
    const bool untouched = stillPoisoned(need);
    Serial.printf("#TINYLIMIT ok=%d untouched=%d\n", ok ? 1 : 0, untouched ? 1 : 0);
    // Either it refuses outright, or the limit is raised to something workable
    // and the full page comes out — but never a page that is partly drawn.
    reportCheck("tiny_limit_no_partial", !ok || !untouched,
                "an unusable memory limit either fails or produces the whole page");
    if (ok) {
      // If it did build, it must be the same bytes as with no limit at all.
      static uint8_t reference[ROW_BYTES * MAX_H];
      Receipt r2(W);
      buildReceipt(r2);
      r2.build(reference, sizeof(reference));
      reportCheck("tiny_limit_same_output",
                  memcmp(reference, g_page, need) == 0,
                  "a clamped memory limit gives the same page as no limit");
    } else {
      reportCheck("tiny_limit_same_output", untouched,
                  "a refused build writes nothing");
    }
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}

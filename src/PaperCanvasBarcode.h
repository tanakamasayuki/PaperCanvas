/*----------------------------------------------------------------------------/
  PaperCanvas - BarcodeKit convenience header.

  Including this pulls in BarcodeKit alongside PaperCanvas, so a barcode can go
  straight onto a page:

      #include <PaperCanvas.h>
      #include <PaperCanvasBarcode.h>

      BarcodeKit::Code128 bc;
      bc.encode("ABC-12345", buf, sizeof(buf));
      r.addBarcode(bc, {.barHeight = 60});

  The placement logic itself lives in <PaperCanvas/Barcode.h> and names no
  encoder: it needs only width() / height() / module() / quiet*() / barExtends(),
  which is BarcodeKit's shape. So <PaperCanvas.h> alone already accepts a
  BarcodeKit symbol — this header exists so callers do not have to include both
  libraries by hand, and so the dependency is declared where it is used.

  https://github.com/tanakamasayuki/PaperCanvas

  Licence: MIT
/----------------------------------------------------------------------------*/
#pragma once

#include <BarcodeKit.h>

#include "PaperCanvas.h"

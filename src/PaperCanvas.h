/*----------------------------------------------------------------------------/
  PaperCanvas - Monochrome 1bpp bitmap generator for receipt and label printers.

  Lays out text, images and barcodes on a receipt (fixed width, height driven by
  content) or a label (fixed canvas) and produces a printer-independent 1bpp
  bitmap. Printer commands, transports and barcode encoding are out of scope.

  https://github.com/tanakamasayuki/PaperCanvas

  Licence: MIT
  Author:  TANAKA Masayuki

  Barcode support lives in the optional <PaperCanvasBarcode.h>; including it is
  what pulls in a dependency on BarcodeKit. This header does not.
/----------------------------------------------------------------------------*/
#pragma once

#include "papercanvas_version.h"

#include "PaperCanvas/Common.h"
#include "PaperCanvas/Element.h"
#include "PaperCanvas/MonoPanel.h"
#include "PaperCanvas/Receipt.h"

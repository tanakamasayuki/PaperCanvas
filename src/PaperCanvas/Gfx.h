/*----------------------------------------------------------------------------/
  PaperCanvas - which graphics header to use.

  LovyanGFX ships as <LovyanGFX.hpp>; M5GFX ships the same `lgfx` namespace as
  <M5GFX.h> and M5Unified pulls that in. They cannot both be included: the
  second one redefines everything the first declared.

  So the rule is: if the sketch already included one, use it. Otherwise pull in
  whichever is installed, preferring LovyanGFX. Getting this wrong shows up as a
  wall of redefinition errors in someone's M5Stack project, which is a poor way
  to find out a header assumed an include order.
/----------------------------------------------------------------------------*/
#pragma once

#if defined(LOVYANGFX_HPP_) || defined(__M5GFX_H__)
// The sketch got there first; use what it chose.
#elif defined(__has_include)
#if __has_include(<LovyanGFX.hpp>)
#include <LovyanGFX.hpp>
#elif __has_include(<M5GFX.h>)
#include <M5GFX.h>
#else
#error "PaperCanvas needs LovyanGFX or M5GFX. Install one, or include it before <PaperCanvas.h>."
#endif
#else
#include <LovyanGFX.hpp>
#endif

// GENERATED from cases.json by gen_cases.py. Do not edit by hand.
#pragma once

#include <LovyanGFX.hpp>

struct ParityCase {
  const char* name;
  const lgfx::IFont* font;
  const char* text;
  float size;
};

#define PARITY_CANVAS_W 320
#define PARITY_CANVAS_H 64

static const ParityCase PARITY_CASES[] = {
    {"font2_ascii", &fonts::Font2, u8"Total 1580", 1.000000f},
    {"font2_size2", &fonts::Font2, u8"Total", 2.000000f},
    {"font2_size15", &fonts::Font2, u8"Total", 1.500000f},
    {"font4_ascii", &fonts::Font4, u8"ABC 123", 1.000000f},
    {"gfx_ascii", &fonts::FreeSans12pt7b, u8"Receipt", 1.000000f},
    {"efont_ja", &fonts::efontJA_16, u8"ご来店ありがとう", 1.000000f},
    {"efont_mixed", &fonts::efontJA_16, u8"コーヒー 480", 1.000000f},
    {"gothic_ja", &fonts::lgfxJapanGothic_16, u8"産地直送トマト", 1.000000f},
    {"gothic_size2", &fonts::lgfxJapanGothic_16, u8"合計", 2.000000f},
    {"efont_punct", &fonts::efontJA_16, u8"￥1,580-（税込）", 1.000000f},
};

static const size_t PARITY_CASE_COUNT = sizeof(PARITY_CASES) / sizeof(PARITY_CASES[0]);

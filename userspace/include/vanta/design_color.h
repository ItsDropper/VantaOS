/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_USER_DESIGN_COLOR_H
#define VANTA_USER_DESIGN_COLOR_H

#include <stdint.h>

/*
 * WCAG relative luminance lookup for 8-bit sRGB channels. Values are rounded
 * to millionths, so the runtime calculation is integer-only and requires no
 * floating-point support or libm in freestanding userspace programs.
 */
static const uint32_t vanta_design_srgb_linear_million[256] = {
    0U, 304U, 607U, 911U, 1214U, 1518U, 1821U, 2125U,
    2428U, 2732U, 3035U, 3347U, 3677U, 4025U, 4391U, 4777U,
    5182U, 5605U, 6049U, 6512U, 6995U, 7499U, 8023U, 8568U,
    9134U, 9721U, 10330U, 10960U, 11612U, 12286U, 12983U, 13702U,
    14444U, 15209U, 15996U, 16807U, 17642U, 18500U, 19382U, 20289U,
    21219U, 22174U, 23153U, 24158U, 25187U, 26241U, 27321U, 28426U,
    29557U, 30713U, 31896U, 33105U, 34340U, 35601U, 36889U, 38204U,
    39546U, 40915U, 42311U, 43735U, 45186U, 46665U, 48172U, 49707U,
    51269U, 52861U, 54480U, 56128U, 57805U, 59511U, 61246U, 63010U,
    64803U, 66626U, 68478U, 70360U, 72272U, 74214U, 76185U, 78187U,
    80220U, 82283U, 84376U, 86500U, 88656U, 90842U, 93059U, 95307U,
    97587U, 99899U, 102242U, 104616U, 107023U, 109462U, 111932U, 114435U,
    116971U, 119538U, 122139U, 124772U, 127438U, 130136U, 132868U, 135633U,
    138432U, 141263U, 144128U, 147027U, 149960U, 152926U, 155926U, 158961U,
    162029U, 165132U, 168269U, 171441U, 174647U, 177888U, 181164U, 184475U,
    187821U, 191202U, 194618U, 198069U, 201556U, 205079U, 208637U, 212231U,
    215861U, 219526U, 223228U, 226966U, 230740U, 234551U, 238398U, 242281U,
    246201U, 250158U, 254152U, 258183U, 262251U, 266356U, 270498U, 274677U,
    278894U, 283149U, 287441U, 291771U, 296138U, 300544U, 304987U, 309469U,
    313989U, 318547U, 323143U, 327778U, 332452U, 337164U, 341914U, 346704U,
    351533U, 356400U, 361307U, 366253U, 371238U, 376262U, 381326U, 386429U,
    391572U, 396755U, 401978U, 407240U, 412543U, 417885U, 423268U, 428690U,
    434154U, 439657U, 445201U, 450786U, 456411U, 462077U, 467784U, 473531U,
    479320U, 485150U, 491021U, 496933U, 502886U, 508881U, 514918U, 520996U,
    527115U, 533276U, 539479U, 545724U, 552011U, 558340U, 564712U, 571125U,
    577580U, 584078U, 590619U, 597202U, 603827U, 610496U, 617207U, 623960U,
    630757U, 637597U, 644480U, 651406U, 658375U, 665387U, 672443U, 679542U,
    686685U, 693872U, 701102U, 708376U, 715694U, 723055U, 730461U, 737910U,
    745404U, 752942U, 760525U, 768151U, 775822U, 783538U, 791298U, 799103U,
    806952U, 814847U, 822786U, 830770U, 838799U, 846873U, 854993U, 863157U,
    871367U, 879622U, 887923U, 896269U, 904661U, 913099U, 921582U, 930111U,
    938686U, 947307U, 955973U, 964686U, 973445U, 982251U, 991102U, 1000000U,
};

static inline uint32_t vanta_design_relative_luminance_million(uint32_t color)
{
    uint32_t red = vanta_design_srgb_linear_million[(color >> 16) & 0xFFU];
    uint32_t green = vanta_design_srgb_linear_million[(color >> 8) & 0xFFU];
    uint32_t blue = vanta_design_srgb_linear_million[color & 0xFFU];
    uint64_t weighted = 2126ULL * red + 7152ULL * green + 722ULL * blue;
    return (uint32_t)((weighted + 5000ULL) / 10000ULL);
}

/* Return the WCAG contrast ratio multiplied by 1000 (e.g. 4500 = 4.5:1). */
static inline uint32_t vanta_design_contrast_ratio_milli(uint32_t foreground,
                                                         uint32_t background)
{
    uint32_t first = vanta_design_relative_luminance_million(foreground);
    uint32_t second = vanta_design_relative_luminance_million(background);
    uint32_t lighter = first > second ? first : second;
    uint32_t darker = first > second ? second : first;
    return (uint32_t)(((uint64_t)(lighter + 50000U) * 1000ULL) /
                      (darker + 50000U));
}

/* Select whichever of black or white has the stronger WCAG contrast. */
static inline uint32_t vanta_design_on_color(uint32_t background)
{
    uint32_t luminance =
        vanta_design_relative_luminance_million(background);
    uint32_t black_ratio = (uint32_t)(
        ((uint64_t)(luminance + 50000U) * 1000ULL) / 50000U);
    uint32_t white_ratio = (uint32_t)(
        (1050000ULL * 1000ULL) / (luminance + 50000U));
    return white_ratio > black_ratio ? 0x00FFFFFFU : 0x00101820U;
}

#endif

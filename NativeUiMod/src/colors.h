#pragma once
// Vehicle paint indices with their Los Santos Customs names and sections, in LS Customs order.
// Source: gtahash.com/tools/vehicle-colors (names from the game's carmodcols).

struct PaintColour { int index; const char* name; };

namespace colours {

inline const PaintColour kClassic[] = {
    {0, "Black"}, {147, "Carbon Black"}, {1, "Graphite"}, {11, "Anthracite Black"},
    {2, "Black Steel"}, {3, "Dark Steel"}, {4, "Silver"}, {5, "Bluish Silver"}, {6, "Rolled Steel"},
    {7, "Shadow Silver"}, {8, "Stone Silver"}, {9, "Midnight Silver"}, {10, "Cast Iron Silver"},
    {27, "Red"}, {28, "Torino Red"}, {29, "Formula Red"}, {150, "Lava Red"}, {30, "Blaze Red"},
    {31, "Grace Red"}, {32, "Garnet Red"}, {33, "Sunset Red"}, {34, "Cabernet Red"},
    {143, "Wine Red"}, {35, "Candy Red"}, {135, "Hot Pink"}, {137, "Pfister Pink"},
    {136, "Salmon Pink"}, {36, "Sunrise Orange"}, {38, "Orange"}, {138, "Bright Orange"},
    {99, "Gold"}, {90, "Bronze"}, {88, "Yellow"}, {89, "Race Yellow"}, {91, "Dew Yellow"},
    {49, "Dark Green"}, {50, "Racing Green"}, {51, "Sea Green"}, {52, "Olive Green"},
    {53, "Bright Green"}, {54, "Gasoline Green"}, {92, "Lime Green"}, {141, "Midnight Blue"},
    {61, "Galaxy Blue"}, {62, "Dark Blue"}, {63, "Saxon Blue"}, {64, "Blue"}, {65, "Mariner Blue"},
    {66, "Harbor Blue"}, {67, "Diamond Blue"}, {68, "Surf Blue"}, {69, "Nautical Blue"},
    {73, "Racing Blue"}, {70, "Ultra Blue"}, {74, "Light Blue"}, {96, "Chocolate Brown"},
    {101, "Bison Brown"}, {95, "Creek Brown"}, {94, "Feltzer Brown"}, {97, "Maple Brown"},
    {103, "Beechwood Brown"}, {104, "Sienna Brown"}, {98, "Saddle Brown"}, {100, "Moss Brown"},
    {102, "Woodbeech Brown"}, {99, "Straw Brown"}, {105, "Sandy Brown"}, {106, "Bleached Brown"},
    {71, "Schafter Purple"}, {72, "Spinnaker Purple"}, {142, "Midnight Purple"},
    {145, "Bright Purple"}, {107, "Cream"}, {111, "Ice White"}, {112, "Frost White"},
};
inline const PaintColour kMetallic[] = {
    {0, "Black"}, {147, "Carbon Black"}, {1, "Graphite"}, {11, "Anthracite Black"},
    {2, "Black Steel"}, {3, "Dark Steel"}, {4, "Silver"}, {5, "Bluish Silver"}, {6, "Rolled Steel"},
    {7, "Shadow Silver"}, {8, "Stone Silver"}, {9, "Midnight Silver"}, {10, "Cast Iron Silver"},
    {27, "Red"}, {28, "Torino Red"}, {29, "Formula Red"}, {150, "Lava Red"}, {30, "Blaze Red"},
    {31, "Grace Red"}, {32, "Garnet Red"}, {33, "Sunset Red"}, {34, "Cabernet Red"},
    {143, "Wine Red"}, {35, "Candy Red"}, {135, "Hot Pink"}, {137, "Pfister Pink"},
    {136, "Salmon Pink"}, {36, "Sunrise Orange"}, {38, "Orange"}, {138, "Bright Orange"},
    {37, "Gold"}, {90, "Bronze"}, {88, "Yellow"}, {89, "Race Yellow"}, {91, "Dew Yellow"},
    {49, "Dark Green"}, {50, "Racing Green"}, {51, "Sea Green"}, {52, "Olive Green"},
    {53, "Bright Green"}, {54, "Gasoline Green"}, {92, "Lime Green"}, {141, "Midnight Blue"},
    {61, "Galaxy Blue"}, {62, "Dark Blue"}, {63, "Saxon Blue"}, {64, "Blue"}, {65, "Mariner Blue"},
    {66, "Harbor Blue"}, {67, "Diamond Blue"}, {68, "Surf Blue"}, {69, "Nautical Blue"},
    {73, "Racing Blue"}, {70, "Ultra Blue"}, {74, "Light Blue"}, {96, "Chocolate Brown"},
    {101, "Bison Brown"}, {95, "Creek Brown"}, {94, "Feltzer Brown"}, {97, "Maple Brown"},
    {103, "Beechwood Brown"}, {104, "Sienna Brown"}, {98, "Saddle Brown"}, {100, "Moss Brown"},
    {102, "Woodbeech Brown"}, {99, "Straw Brown"}, {105, "Sandy Brown"}, {106, "Bleached Brown"},
    {71, "Schafter Purple"}, {72, "Spinnaker Purple"}, {146, "Midnight Purple"},
    {145, "Bright Purple"}, {107, "Cream"}, {111, "Ice White"}, {112, "Frost White"},
};
inline const PaintColour kMatte[] = {
    {12, "Black"}, {13, "Gray"}, {14, "Light Gray"}, {131, "Ice White"}, {83, "Blue"},
    {82, "Dark Blue"}, {84, "Midnight Blue"}, {149, "Midnight Purple"}, {148, "Schafter Purple"},
    {39, "Red"}, {40, "Dark Red"}, {41, "Orange"}, {42, "Yellow"}, {55, "Lime Green"},
    {128, "Green"}, {151, "Forest Green"}, {155, "Foliage Green"}, {152, "Olive Drab"},
    {153, "Dark Earth"}, {154, "Desert Tan"},
};
inline const PaintColour kMetals[] = {
    {117, "Brushed Steel"}, {118, "Brushed Black Steel"}, {119, "Brushed Aluminum"},
    {158, "Pure Gold"}, {159, "Brushed Gold"},
};
inline const PaintColour kChrome[] = {
    {120, "Chrome"},
};
inline const PaintColour kPearlescent[] = {
    {0, "Black"}, {1, "Graphite"}, {11, "Anthracite Black"}, {2, "Black Steel"}, {3, "Dark Steel"},
    {4, "Silver"}, {5, "Bluish Silver"}, {6, "Rolled Steel"}, {7, "Shadow Silver"},
    {8, "Stone Silver"}, {9, "Midnight Silver"}, {10, "Cast Iron Silver"}, {27, "Red"},
    {28, "Torino Red"}, {29, "Formula Red"}, {150, "Lava Red"}, {30, "Blaze Red"},
    {31, "Grace Red"}, {32, "Garnet Red"}, {33, "Sunset Red"}, {34, "Cabernet Red"},
    {143, "Wine Red"}, {35, "Candy Red"}, {135, "Hot Pink"}, {137, "Pfister Pink"},
    {136, "Salmon Pink"}, {36, "Sunrise Orange"}, {38, "Orange"}, {138, "Bright Orange"},
    {37, "Gold"}, {90, "Bronze"}, {88, "Yellow"}, {89, "Race Yellow"}, {91, "Dew Yellow"},
    {49, "Dark Green"}, {50, "Racing Green"}, {51, "Sea Green"}, {52, "Olive Green"},
    {53, "Bright Green"}, {54, "Gasoline Green"}, {92, "Lime Green"}, {141, "Midnight Blue"},
    {61, "Galaxy Blue"}, {62, "Dark Blue"}, {63, "Saxon Blue"}, {64, "Blue"}, {65, "Mariner Blue"},
    {66, "Harbor Blue"}, {67, "Diamond Blue"}, {68, "Surf Blue"}, {69, "Nautical Blue"},
    {73, "Racing Blue"}, {70, "Ultra Blue"}, {74, "Light Blue"}, {96, "Chocolate Brown"},
    {101, "Bison Brown"}, {95, "Creek Brown"}, {94, "Feltzer Brown"}, {97, "Maple Brown"},
    {103, "Beechwood Brown"}, {104, "Sienna Brown"}, {98, "Saddle Brown"}, {100, "Moss Brown"},
    {102, "Woodbeech Brown"}, {99, "Straw Brown"}, {105, "Sandy Brown"}, {106, "Bleached Brown"},
    {71, "Schafter Purple"}, {72, "Spinnaker Purple"}, {142, "Midnight Purple"},
    {145, "Bright Purple"}, {107, "Cream"}, {111, "Ice White"}, {112, "Frost White"},
};
inline const PaintColour kChameleon[] = {
    {161, "Anodized Red Pearl"}, {164, "Anodized Blue Pearl"}, {170, "Anodized Gold Pearl"},
    {171, "Green/Blue Flip"}, {183, "Purple/Green Flip"}, {191, "Orange/Purple Flip"},
    {199, "Dark Purple Pearl"}, {209, "Baby Blue Pearl"}, {216, "Red Prismatic Pearl"},
    {218, "Black Prismatic Pearl"}, {162, "Anodized Burgundy Pearl"},
    {163, "Anodized Purple Pearl"}, {165, "Anodized Green Pearl"}, {166, "Anodized Lime Pearl"},
    {167, "Anodized Copper Pearl"}, {168, "Anodized Bronze Pearl"},
    {169, "Anodized Champagne Pearl"}, {172, "Green/Red Flip"}, {173, "Green/Brown Flip"},
    {174, "Green/Turquoise Flip"}, {175, "Green/Purple Flip"}, {176, "Teal/Purple Flip"},
    {177, "Turquoise/Red Flip"}, {178, "Turquoise/Purple Flip"}, {179, "Cyan/Red Flip"},
    {180, "Blue/Pink Flip"}, {181, "Blue/Green Flip"}, {182, "Purple/Red Flip"},
    {184, "Magenta/Green Flip"}, {185, "Magenta/Yellow Flip"}, {186, "Burgundy/Green Flip"},
    {187, "Magenta/Cyan Flip"}, {188, "Copper/Purple Flip"}, {189, "Magenta/Orange Flip"},
    {190, "Red/Orange Flip"}, {192, "Orange/Blue Flip"}, {193, "White/Purple Flip"},
    {194, "Red Rainbow Flip"}, {195, "Blue Rainbow Flip"}, {196, "Dark Green Pearl"},
    {197, "Dark Teal Pearl"}, {198, "Dark Blue Pearl"}, {200, "Oil Slick Pearl"},
    {201, "Light Green Pearl"}, {202, "Light Blue Pearl"}, {203, "Light Purple Pearl"},
    {204, "Light Pink Pearl"}, {205, "Off White Pearl"}, {206, "Soft Pink Pearl"},
    {207, "Pastel Yellow Pearl"}, {208, "Pastel Green Pearl"}, {210, "Cream Pearl"},
    {211, "White Prismatic Pearl"}, {212, "Graphite Prismatic Pearl"},
    {213, "Blue Prismatic Pearl"}, {214, "Purple Prismatic Pearl"}, {215, "Pink Prismatic Pearl"},
    {217, "Green Prismatic Pearl"}, {219, "Black Oil Spill Prismatic"},
    {220, "Black Rainbow Prismatic"}, {221, "Dark Holographic"}, {222, "Light Holographic"},
};

struct Section { const char* name; const PaintColour* list; int count; };
template <int N> constexpr Section Make(const char* n, const PaintColour (&a)[N]) { return {n, a, N}; }
inline const Section kSections[] = {
    Make("Classic", kClassic), Make("Metallic", kMetallic), Make("Matte", kMatte),
    Make("Metals", kMetals), Make("Chrome", kChrome), Make("Chameleon", kChameleon),
};
inline const Section kPearlSection = Make("Pearlescent", kPearlescent);

// Xenon headlight colours (index for SET_VEHICLE_XENON_LIGHT_COLOR_INDEX; -1/255 = stock).
inline const char* const kXenon[] = {
    "White", "Blue", "Electric Blue", "Mint Green", "Lime Green", "Yellow", "Golden Shower",
    "Orange", "Red", "Pony Pink", "Hot Pink", "Purple", "Blacklight",
};
// Window tints, in LS Customs order: {tint index, name}.
inline const PaintColour kTints[] = {
    {0, "None"}, {4, "Stock"}, {3, "Light Smoke"}, {2, "Dark Smoke"}, {1, "Pure Black"}, {5, "Limo"},
    {6, "Green"},
};

}  // namespace colours

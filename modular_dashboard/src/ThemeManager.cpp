#include "ThemeManager.h"
#include "DynamicSpeedometerCharts.h"

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {
    // Define speedometer color schemes for each theme
    auto darkSpeedometer = SpeedometerColors{
        QColor(40,44,52),      // background
        QColor(100,100,100),   // arcBase  
        QColor(255,255,255),   // needleNormal
        QColor(76,175,80),     // zoneGood (green)
        QColor(255,193,7),     // zoneWarn (yellow)
        QColor(244,67,54),     // zoneDanger (red)
        QColor(255,255,255),   // text
        QColor(0,255,180)      // glow
    };
    
    auto lightSpeedometer = SpeedometerColors{
        QColor(250,250,250),   // background
        QColor(150,150,150),   // arcBase
        QColor(50,50,50),      // needleNormal  
        QColor(56,142,60),     // zoneGood
        QColor(255,152,0),     // zoneWarn
        QColor(211,47,47),     // zoneDanger
        QColor(33,33,33),      // text
        QColor(33,150,243)     // glow
    };
    
    auto monokaiSpeedometer = SpeedometerColors{
        QColor(39,40,34),      // background
        QColor(117,113,94),    // arcBase
        QColor(248,248,242),   // needleNormal
        QColor(166,226,46),    // zoneGood (monokai green)
        QColor(253,151,31),    // zoneWarn (monokai orange)
        QColor(249,38,114),    // zoneDanger (monokai pink)
        QColor(248,248,242),   // text
        QColor(166,226,46)     // glow
    };
    
    auto draculaSpeedometer = SpeedometerColors{
        QColor(40,42,54),      // background
        QColor(98,114,164),    // arcBase
        QColor(248,248,242),   // needleNormal
        QColor(80,250,123),    // zoneGood (dracula green)
        QColor(241,250,140),   // zoneWarn (dracula yellow)
        QColor(255,85,85),     // zoneDanger (dracula red)
        QColor(248,248,242),   // text
        QColor(189,147,249)    // glow (dracula purple)
    };
    
    auto nordSpeedometer = SpeedometerColors{
        QColor(46,52,64),      // background
        QColor(76,86,106),     // arcBase
        QColor(216,222,233),   // needleNormal
        QColor(163,190,140),   // zoneGood (nord green)
        QColor(235,203,139),   // zoneWarn (nord yellow)
        QColor(191,97,106),    // zoneDanger (nord red)
        QColor(216,222,233),   // text
        QColor(136,192,208)    // glow (nord frost)
    };
    
    // 10 themes with speedometer colors
    add({"Dark", "QWidget { background: #202429; color: #e0e0e0; }", QColor(0x20,0x24,0x29), QColor(0xE0,0xE0,0xE0), QColor(0x4C,0xAF,0x50), QColor(80,80,80), darkSpeedometer});
    add({"Light", "QWidget { background: #FAFAFA; color: #111; }", QColor(0xFA,0xFA,0xFA), QColor(0x11,0x11,0x11), QColor(0x21,0x96,0xF3), QColor(200,200,200), lightSpeedometer});
    add({"Monokai", "QWidget { background: #272822; color: #F8F8F2; }", QColor(0x27,0x28,0x22), QColor(0xF8,0xF8,0xF2), QColor(0xA6,0xE2,0x2E), QColor(100,100,100), monokaiSpeedometer});
    add({"Dracula", "QWidget { background: #282a36; color: #f8f8f2; }", QColor(0x28,0x2A,0x36), QColor(0xF8,0xF8,0xF2), QColor(0xBD,0x93,0xF9), QColor(90,90,100), draculaSpeedometer});
        add({"Nord", "QWidget { background: #2E3440; color: #D8DEE9; }", QColor(0x2E,0x34,0x40), QColor(0xD8,0xDE,0xE9), QColor(0x88,0xC0,0xD0), QColor(80,90,100), nordSpeedometer});
    // Use variations for the remaining themes with proper SpeedometerColors constructor
    SpeedometerColors solarizedDark = {QColor(0,43,54), QColor(88,110,117), QColor(147,161,161), QColor(133,153,0), QColor(181,137,0), QColor(220,50,47), QColor(147,161,161), QColor(42,161,152)};
    SpeedometerColors solarizedLight = {QColor(253,246,227), QColor(147,161,161), QColor(101,123,131), QColor(133,153,0), QColor(181,137,0), QColor(220,50,47), QColor(101,123,131), QColor(38,139,210)};
    SpeedometerColors gruvbox = {QColor(40,40,40), QColor(124,111,100), QColor(235,219,178), QColor(184,187,38), QColor(250,189,47), QColor(251,73,52), QColor(235,219,178), QColor(254,128,25)};
    SpeedometerColors highContrast = {QColor(0,0,0), QColor(128,128,128), QColor(255,255,255), QColor(0,255,0), QColor(255,255,0), QColor(255,0,0), QColor(255,255,255), QColor(255,0,87)};
    
    add({"Solarized Dark", "QWidget { background: #002b36; color: #93a1a1; }", QColor(0x00,0x2B,0x36), QColor(0x93,0xA1,0xA1), QColor(0xB5,0x89,0x00), QColor(50,80,90), solarizedDark});
    add({"Solarized Light", "QWidget { background: #fdf6e3; color: #657b83; }", QColor(0xFD,0xF6,0xE3), QColor(0x65,0x7B,0x83), QColor(0x26,0x8B,0xD2), QColor(180,160,120), solarizedLight});
    add({"Gruvbox Dark", "QWidget { background: #282828; color: #ebdbb2; }", QColor(0x28,0x28,0x28), QColor(0xEB,0xDB,0xB2), QColor(0xFE,0x80,0x19), QColor(90,80,60), gruvbox});
    add({"High Contrast", "QWidget { background: #000000; color: #FFFFFF; }", QColor(0x00,0x00,0x00), QColor(0xFF,0xFF,0xFF), QColor(0xFF,0x00,0x57), QColor(120,120,120), highContrast});
    
    // Additional modern themes
    SpeedometerColors tokyoNight = {QColor(26,27,38), QColor(69,71,90), QColor(169,177,214), QColor(158,206,106), QColor(224,175,104), QColor(247,118,142), QColor(169,177,214), QColor(125,207,255)};
    add({"Tokyo Night", "QWidget { background: #1a1b26; color: #a9b1d6; }", QColor(0x1A,0x1B,0x26), QColor(0xA9,0xB1,0xD6), QColor(0x7D,0xCF,0xFF), QColor(50,55,70), tokyoNight});
    
    SpeedometerColors cyberpunk = {QColor(16,0,43), QColor(138,43,226), QColor(255,20,147), QColor(0,255,127), QColor(255,215,0), QColor(255,69,0), QColor(255,20,147), QColor(148,0,211)};
    add({"Cyberpunk", "QWidget { background: #10002b; color: #ff1493; }", QColor(0x10,0x00,0x2B), QColor(0xFF,0x14,0x93), QColor(0x94,0x00,0xD3), QColor(80,0,120), cyberpunk});
    
    // Removed old "Pastel", "Oceanic", and "Minimal White"; replacing with KiloCode collection below
    
    SpeedometerColors retro = {QColor(43,45,66), QColor(141,161,99), QColor(238,238,238), QColor(184,187,38), QColor(254,128,25), QColor(204,36,29), QColor(238,238,238), QColor(152,151,26)};
    add({"Retro", "QWidget { background: #2b2d42; color: #eee; }", QColor(0x2B,0x2D,0x42), QColor(0xEE,0xEE,0xEE), QColor(0x98,0x97,0x1A), QColor(90,100,110), retro});
    
    // ----- KiloCode Signature Themes (7) -----
    SpeedometerColors kcAbyss = {QColor(10,10,16), QColor(30,38,51), QColor(0,245,255), QColor(0,160,160), QColor(0,208,208), QColor(0,245,255), QColor(230,247,255), QColor(0,245,255)};
    add({"KiloCode Abyss", "QWidget { background: #0A0A10; color: #E6F7FF; }", QColor(0x0A,0x0A,0x10), QColor(0xE6,0xF7,0xFF), QColor(0x00,0xF5,0xFF), QColor(34,48,64), kcAbyss});

    SpeedometerColors kcAurora = {QColor(14,14,24), QColor(70,80,120), QColor(180,220,255), QColor(95,200,170), QColor(255,190,120), QColor(247,118,142), QColor(210,220,255), QColor(125,207,255)};
    add({"KiloCode Aurora", "QWidget { background: #0E0E18; color: #D2DCFF; }", QColor(0x0E,0x0E,0x18), QColor(0xD2,0xDC,0xFF), QColor(0x7D,0xCF,0xFF), QColor(60,70,110), kcAurora});

    SpeedometerColors kcNeonMint = {QColor(12,18,16), QColor(40,70,60), QColor(180,255,220), QColor(80,220,170), QColor(255,210,90), QColor(255,95,110), QColor(220,255,235), QColor(90,255,210)};
    add({"KiloCode NeonMint", "QWidget { background: #0C1210; color: #DCFFEB; }", QColor(0x0C,0x12,0x10), QColor(0xDC,0xFF,0xEB), QColor(0x5A,0xFF,0xD2), QColor(50,80,70), kcNeonMint});

    SpeedometerColors kcSunset = {QColor(18,12,16), QColor(120,70,90), QColor(255,210,160), QColor(252,92,101), QColor(255,177,66), QColor(255,75,43), QColor(255,220,200), QColor(255,136,102)};
    add({"KiloCode Sunset", "QWidget { background: #120C10; color: #FFDCC8; }", QColor(0x12,0x0C,0x10), QColor(0xFF,0xDC,0xC8), QColor(0xFF,0x88,0x66), QColor(90,50,60), kcSunset});

    SpeedometerColors kcGraphite = {QColor(12,12,14), QColor(90,90,100), QColor(220,220,230), QColor(120,200,160), QColor(240,200,110), QColor(230,110,110), QColor(230,230,240), QColor(140,200,255)};
    add({"KiloCode Graphite", "QWidget { background: #0C0C0E; color: #E6E6F0; }", QColor(0x0C,0x0C,0x0E), QColor(0xE6,0xE6,0xF0), QColor(0x8C,0xC8,0xFF), QColor(70,70,80), kcGraphite});

    SpeedometerColors kcDeepOcean = {QColor(8,14,22), QColor(60,80,105), QColor(190,210,230), QColor(0,155,200), QColor(0,195,230), QColor(255,115,120), QColor(200,220,240), QColor(0,200,255)};
    add({"KiloCode DeepOcean", "QWidget { background: #080E16; color: #C8D6E4; }", QColor(0x08,0x0E,0x16), QColor(0xC8,0xD6,0xE4), QColor(0x00,0xC8,0xFF), QColor(40,60,88), kcDeepOcean});

    SpeedometerColors kcSolarCyan = {QColor(14,16,18), QColor(90,110,120), QColor(0,245,255), QColor(0,160,180), QColor(0,200,210), QColor(255,95,110), QColor(220,240,255), QColor(0,245,255)};
    add({"KiloCode SolarCyan", "QWidget { background: #0E1012; color: #DCEFFF; }", QColor(0x0E,0x10,0x12), QColor(0xDC,0xEF,0xFF), QColor(0x00,0xF5,0xFF), QColor(70,90,100), kcSolarCyan});
    currentName = "Dark";
}

void ThemeManager::add(const Theme& t) { themes.insert(t.name, t); }

const Theme& ThemeManager::themeByName(const QString& name) const {
    auto it = themes.find(name);
    if (it != themes.end()) return it.value();
    return themes.first();
}

QStringList ThemeManager::themeNames() const { return themes.keys(); }

void ThemeManager::setCurrent(const QString& name) { if (themes.contains(name)) currentName = name; }

void ThemeManager::setTheme(ColorTheme theme) {
    QStringList names = themeNames();
    int index = static_cast<int>(theme);
    if (index >= 0 && index < names.size()) {
        setCurrent(names[index]);
    }
}

SpeedometerColors ThemeManager::getSpeedometerColors(ColorTheme theme) const {
    QStringList names = themeNames();
    int index = static_cast<int>(theme);
    if (index >= 0 && index < names.size()) {
        return themeByName(names[index]).speedometer;
    }
    return SpeedometerColors{};
}

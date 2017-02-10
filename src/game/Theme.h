#pragma once

#include "system/Color.h"

#include <string>


struct ThemeColors {
    RGBAColor primary;
    RGBAColor text;
    RGBAColor text_accent;
    RGBAColor panel;

    ThemeColors()
        : primary(0x2030FFFF_rgba)
        , text(0xEEEEEEFF_rgba)
        , text_accent(0xFFA500FF_rgba)
        , panel(0x0A0AFF80_rgba)
    {}
};

struct GameplayTheme {
    bool draw_wellbg;
    bool draw_labels;
    bool draw_panels;

    GameplayTheme()
        : draw_wellbg(true)
        , draw_labels (true)
        , draw_panels(true)
    {}
};

struct ThemeConfig {
    ThemeColors colors;
    GameplayTheme gameplay;

    void set_theme_dir(const std::string&);

    std::string config_path() const { return m_config_path; }

    std::string random_menu_music() const;
    std::string random_game_music() const;
    std::string random_game_background() const;

    std::string get_sfx(const std::string&) const;
    std::string get_texture(const std::string&) const;

private:
    std::string m_theme_dir;
    std::string m_config_path;

    std::string resolve_path(const std::string&) const;
    std::string random_file_from(const std::string&) const;
};

class ThemeConfigFile {
public:
    static ThemeConfig load(const std::string& path);
};
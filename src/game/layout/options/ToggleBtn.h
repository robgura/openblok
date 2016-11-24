#pragma once

#include "game/layout/options/OptionsItem.h"

#include <array>
#include <memory>

class AppContext;
class Texture;


namespace Layout {
namespace Options {

class ToggleButton : public OptionsItem {
public:
    ToggleButton(AppContext&, bool initial_state, std::string&& label);

    void setPosition(int x, int y) override;
    void draw(GraphicsContext&) const override;

private:
    std::array<std::unique_ptr<Texture>, 2> tex_onoff;
    int padding;
    bool switch_state;
};

} // namespace Options
} // namespace Layout
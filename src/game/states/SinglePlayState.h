#pragma once

#include "game/GameState.h"


class SinglePlayState: public GameState {
public:
    SinglePlayState(AppContext&);
    void update(const std::vector<InputEvent>&, AppContext&) final;
    void draw(GraphicsContext&) final;
};

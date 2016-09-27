#include "Well.h"

#include "GameplayResources.h"
#include "MinoFactory.h"
#include "PieceFactory.h"
#include "game/GameState.h"
#include "game/WellEvent.h"
#include "system/EventCollector.h"
#include "system/GraphicsContext.h"
#include "system/InputEvent.h"

#include <assert.h>


namespace WellUtil {

CellLockAnim::CellLockAnim(unsigned row, unsigned col)
    : cell_x(col * Mino::texture_size_px)
    , cell_y_top(row * Mino::texture_size_px)
    , cell_y_bottom(cell_y_top + Mino::texture_size_px)
    , anim_y_top(GameState::frame_duration * 20, [this](double t){
        return this->cell_y_bottom - t * Mino::texture_size_px * 2;
    })
{}

void CellLockAnim::update(Duration t) {
    anim_y_top.update(t);
}

void CellLockAnim::draw(GraphicsContext& gcx, int offset_x, int offset_y) const {
    static const int effect_height_max = Mino::texture_size_px / 2;
    int y_top = std::max<int>(anim_y_top.value(), cell_y_top);
    int y_bottom = std::min<int>(anim_y_top.value() + effect_height_max, cell_y_bottom);
    if (y_bottom <= y_top)
        return;

    gcx.drawFilledRect({
        static_cast<int>(offset_x + cell_x), offset_y + y_top,
        Mino::texture_size_px, y_bottom - y_top},
        0xFFFFFF30_rgba);
}

} // namespace WellUtil


Well::Well()
    : gameover(false)
    , active_piece_x(0)
    , active_piece_y(0)
    , ghost_piece_y(0)
    , gravity_delay(GameState::frame_duration * 64)
    , gravity_timer(Duration::zero())
    , horizontal_delay_normal(GameState::frame_duration * 14)
    , horizontal_delay_turbo(GameState::frame_duration * 4)
    , horizontal_delay_current(horizontal_delay_normal)
    , horizontal_timer(Duration::zero())
    , das_timer(horizontal_delay_normal)
    , softdrop_delay(horizontal_delay_turbo)
    , softdrop_timer(Duration::zero())
    , rotation_delay(GameState::frame_duration * 12)
    , rotation_timer(Duration::zero())
    , harddrop_locks_instantly(true)
    , lock_infinity(true)
    , lock_promise(GameState::frame_duration * 30, [](double){}, [this](){
            this->lockThenRequestNext();
        })
    , lineclear_alpha(GameState::frame_duration * 40, [](double t){
            return static_cast<uint8_t>((1.0 - t) * 0xFF);
        },
        [this](){
            this->removeEmptyRows();
            this->lock_promise.stop();
            this->notify(WellEvent(WellEvent::Type::NEXT_REQUESTED));
        })
{
    keystates[InputType::LEFT] = false;
    keystates[InputType::RIGHT] = false;
    keystates[InputType::DOWN] = false;
    keystates[InputType::A] = false;
    keystates[InputType::B] = false;
    previous_keystates = keystates;

    lineclear_alpha.stop();
}

void Well::update(const std::vector<InputEvent>& events, AppContext&)
{
    for (auto& anim : onlock_anims)
        anim.update(GameState::frame_duration);
    onlock_anims.remove_if([](WellUtil::CellLockAnim& anim){ return !anim.isActive(); });

    if (gameover)
        return;

    if (pending_cleared_rows.size()) {
        assert(lineclear_alpha.running());
        lineclear_alpha.update(GameState::frame_duration);
        return;
    }

    updateKeystate(events);
    handleKeys(events);

    if (!active_piece)
        return;

    updateGravity();
    updateLockDelay();
}

void Well::updateKeystate(const std::vector<InputEvent>& events)
{
    previous_keystates = keystates;

    for (const auto& event : events)
        keystates[event.type()] = event.down();
}

void Well::handleKeys(const std::vector<InputEvent>& events)
{
    // keep it true only if down key is still down
    skip_gravity = (keystates.at(InputType::DOWN) && previous_keystates.at(InputType::DOWN));

    // for some events onpress/onrelease handling is better suited
    for (const auto& event : events) {
        // press
        if (event.down()) {
            switch (event.type()) {
            case InputType::UP:
            case InputType::GAME_HARDDROP:
                hardDrop();
                skip_gravity = true;
                break;

            case InputType::GAME_HOLD:
                notify(WellEvent(WellEvent::Type::HOLD_REQUESTED));
                skip_gravity = true;
                break;

            default:
                break;
            }
        }
        // release
        else {
            switch (event.type()) {
            case InputType::LEFT:
            case InputType::RIGHT:
                resetDAS();
                break;
            default:
                break;
            }
        }
    }


    rotation_timer -= GameState::frame_duration;
    if (keystates.at(InputType::A) != keystates.at(InputType::B) && rotation_timer <= Duration::zero()) {
        if (keystates.at(InputType::A))
            rotateCCWNow();
        else
            rotateCWNow();

        rotation_timer = rotation_delay;
    }

    horizontal_timer -= GameState::frame_duration;
    if (horizontal_timer <= Duration::zero()) {
        if (keystates.at(InputType::LEFT) != keystates.at(InputType::RIGHT)) {
            if (keystates.at(InputType::LEFT))
                moveLeftNow();
            else
                moveRightNow();


            // update DAS
            das_timer -= horizontal_delay_normal;
            if (das_timer < Duration::zero())
                horizontal_delay_current = horizontal_delay_turbo;

            horizontal_timer = horizontal_delay_current;
        }
    }

    softdrop_timer -= GameState::frame_duration;
    if (keystates.at(InputType::DOWN) && softdrop_timer <= Duration::zero()) {
        moveDownNow();
        skip_gravity = true;
        softdrop_timer = softdrop_delay;
    }
}

void Well::resetDAS()
{
    das_timer = horizontal_delay_normal;
    horizontal_delay_current = horizontal_delay_normal;
}

void Well::resetInput()
{
    resetDAS();
    for (auto& key : keystates)
        key.second = false;
}

void Well::updateGravity()
{
    gravity_timer += GameState::frame_duration;
    if (gravity_timer >= gravity_delay) {
        gravity_timer -= gravity_delay;

        // do not apply downward movement twice
        if (!skip_gravity)
            applyGravity();
    }
}

void Well::updateLockDelay()
{
    if (isOnGround())
        lock_promise.unpause();
    else
        lock_promise.stop();
    lock_promise.update(GameState::frame_duration);
}

void Well::addPiece(Piece::Type type)
{
    // the player can only control one piece at a time
    assert(!active_piece);

    active_piece = PieceFactory::make_uptr(type);
    active_piece_x = 3;

    // try to place the piece in row 20, then move up if it fails
    for (active_piece_y = 3; active_piece_y > 0; /* note: unsigned */ ) {
        if (!hasCollisionAt(active_piece_x, --active_piece_y)) {
            calculateGhostOffset();
            return;
        }
    }

    // couldn't place the piece, game over
    lockAndReleasePiece();
    gameover = true;
}

void Well::deletePiece()
{
    active_piece = nullptr;
}

void Well::setGravity(unsigned frames)
{
    gravity_delay = frames * GameState::frame_duration;
}

bool Well::hasCollisionAt(int offset_x, unsigned offset_y)
{
    // At least one line of the piece grid must be on the board.
    // Horizontally, a piece can go between -3 and width+3,
    // vertically from 0 to heigh+3 (it cannot be over the board)
    assert(offset_x + 3 >= 0 && offset_x < static_cast<int>(matrix[0].size()));
    assert(offset_y < matrix.size());
    assert(active_piece);

    size_t piece_gridx = 0, piece_gridy = 0;
    for (unsigned row = offset_y; row <= offset_y + 3; row++) {
        for (int cell = offset_x; cell <= offset_x + 3; cell++) {
            bool board_has_mino_here = true;
            if (row < matrix.size() && cell >= 0 && cell < static_cast<int>(matrix[0].size()))
                board_has_mino_here = matrix.at(row).at(cell).operator bool();

            bool piece_has_mino_here = active_piece->currentGrid()
                                       .at(piece_gridy).at(piece_gridx).operator bool();

            if (piece_has_mino_here && board_has_mino_here)
                return true;

            piece_gridx++;
        }
        piece_gridy++;
        piece_gridx = 0;
    }

    return false;
}

void Well::calculateGhostOffset()
{
    assert(active_piece);

    ghost_piece_y = active_piece_y;
    while (ghost_piece_y + 1u < matrix.size() && !hasCollisionAt(active_piece_x, ghost_piece_y + 1))
        ghost_piece_y++;
}

void Well::applyGravity()
{
    moveDownNow();
}

void Well::moveLeftNow()
{
    if (!active_piece || active_piece_x - 1 <= -3)
        return;

    if (!hasCollisionAt(active_piece_x - 1, active_piece_y)) {
        active_piece_x--;
        calculateGhostOffset();

        if (lock_infinity)
            lock_promise.stop();
    }
}

void Well::moveRightNow()
{
    if (!active_piece || active_piece_x + 1 >= static_cast<int>(matrix[0].size()))
        return;

    if (!hasCollisionAt(active_piece_x + 1, active_piece_y)) {
        active_piece_x++;
        calculateGhostOffset();

        if (lock_infinity)
            lock_promise.stop();
    }
}

bool Well::isOnGround()
{
    assert(active_piece);
    assert(active_piece_y + 1u < matrix.size());

    return hasCollisionAt(active_piece_x, active_piece_y + 1);
}

void Well::moveDownNow()
{
    if (!active_piece || active_piece_y + 1u >= matrix.size())
        return;

    if (!isOnGround())
        active_piece_y++;
    else if (!harddrop_locks_instantly && lock_promise.running()) {
        // sonic drop on-demand lock
        lockThenRequestNext();
    }
}

void Well::hardDrop()
{
    assert(active_piece);

    active_piece_y = ghost_piece_y;
    moveDownNow();
    if (harddrop_locks_instantly)
        lockThenRequestNext();
}

bool Well::placeByWallKick()
{
    assert(active_piece);

    // try at the same row first, then floor kick
    // I pieces can kick higher
    // TODO: check max width/height instead of piece type
    for (unsigned floor = 0; floor < (active_piece->type() == Piece::Type::I ? 3 : 2); floor++) {
        // try 1 tile right
        if (!hasCollisionAt(active_piece_x + 1, active_piece_y - floor)) {
            active_piece_x++;
            active_piece_y -= floor;
            return true;
        }
        // try 1 tile left
        if (!hasCollisionAt(active_piece_x - 1, active_piece_y - floor)) {
            active_piece_x--;
            active_piece_y -= floor;
            return true;
        }
        // if I piece, also try 2 tiles left/right
        if (active_piece->type() == Piece::Type::I) {
            if (active_piece_x + 2 < static_cast<int>(matrix.at(0).size())
                && !hasCollisionAt(active_piece_x + 2, active_piece_y - floor)) {
                active_piece_x += 2;
                active_piece_y -= floor;
                return true;
            }
            if (active_piece_x - 2 >= 0
                && !hasCollisionAt(active_piece_x - 2, active_piece_y - floor)) {
                active_piece_x -= 2;
                active_piece_y -= floor;
                return true;
            }
        }
    }

    return false;
}

void Well::rotateCWNow()
{
    if (!active_piece)
        return;

    active_piece->rotateCW();
    if (hasCollisionAt(active_piece_x, active_piece_y)) {
        if (!placeByWallKick()) {
            active_piece->rotateCCW();
            return;
        }
    }

    calculateGhostOffset();

    if (lock_infinity)
        lock_promise.stop();
}

void Well::rotateCCWNow()
{
    if (!active_piece)
        return;

    active_piece->rotateCCW();
    if (hasCollisionAt(active_piece_x, active_piece_y)) {
        if (!placeByWallKick()) {
            active_piece->rotateCW();
            return;
        }
    }

    calculateGhostOffset();

    if (lock_infinity)
        lock_promise.stop();
}

void Well::lockThenRequestNext()
{
    lockAndReleasePiece();
    if (!gameover && !lineclear_alpha.running())
        notify(WellEvent(WellEvent::Type::NEXT_REQUESTED));
}

void Well::lockAndReleasePiece()
{
    for (unsigned row = 0; row < 4; row++) {
        for (unsigned cell = 0; cell < 4; cell++) {
            if (active_piece_y + row >= matrix.size() ||
                active_piece_x + cell >= matrix.at(0).size() ||
                active_piece_x + static_cast<int>(cell) < 0)
                continue;

            if (active_piece->currentGrid().at(row).at(cell)) {
                matrix[active_piece_y + row][active_piece_x + cell].swap(
                    active_piece->currentGridMut()[row][cell]
                );

                if (active_piece_y + row >= 2)
                    onlock_anims.emplace_back(active_piece_y + row - 2, active_piece_x + cell);
            }
        }
    }

    active_piece.reset();
    lock_promise.stop();
    notify(WellEvent(WellEvent::Type::PIECE_LOCKED));
    checkLineclear();
}

void Well::checkLineclear()
{
    assert(!active_piece);

    for (unsigned row = 0; row < matrix.size(); row++) {
        bool row_filled = true;
        for (auto& cell : matrix[row]) {
            if (!cell) {
                row_filled = false;
                break;
            }
        }

        if (row_filled)
            pending_cleared_rows.insert(row);
    }

    assert(pending_cleared_rows.size() <= 4); // you can clear only 4 rows at once
    if (pending_cleared_rows.size()) {
        for (auto row : pending_cleared_rows) {
            for (auto& cell : matrix[row])
                cell = nullptr;
        }

        lineclear_alpha.restart();
        resetInput();
    }
}

void Well::removeEmptyRows()
{
    // this function should be called if there are empty rows
    assert(pending_cleared_rows.size());
    assert(pending_cleared_rows.size() <= 4);

    WellEvent clear_event(WellEvent::Type::LINE_CLEAR);
    clear_event.count = pending_cleared_rows.size();
    notify(clear_event);

    for (int row = matrix.size(); row >= 0; row--) {
        if (!pending_cleared_rows.count(row))
            continue;

        int next_filled_row = row;
        while (pending_cleared_rows.count(next_filled_row))
            next_filled_row--;

        if (next_filled_row < 0)
            break;

        matrix[row].swap(matrix[next_filled_row]);
        pending_cleared_rows.insert(next_filled_row);
    }

    pending_cleared_rows.clear();
}

void Well::notify(const WellEvent& event)
{
    for (const auto& obs : observers[static_cast<uint8_t>(event.type)])
        obs(event);
}

#ifndef NDEBUG

void Well::fromAscii(const std::string& text)
{
    assert(text.length() == matrix.size() * (matrix[0].size() + 1));

    unsigned str_i = 0;
    for (unsigned row = 0; row < matrix.size(); row++) {
        for (unsigned cell = 0; cell < matrix[0].size(); cell++) {
            if (text.at(str_i) == '.')
                matrix[row][cell].release();
            else
                matrix[row][cell] = MinoFactory::make_uptr(Piece::typeFromAscii(text.at(str_i)));

            str_i++;
        }
        // newline skip
        str_i++;
    }
}

std::string Well::asAscii()
{
    // the piece must be inside the grid, at least partially
    assert(0 <= active_piece_x + 3);
    assert(active_piece_x < static_cast<int>(matrix[0].size()));
    assert(active_piece_y < matrix.size());

    std::string board_layer;
    std::string piece_layer;

    // print board
    for (size_t row = 0; row < matrix.size(); row++) {
        for (size_t cell = 0; cell < matrix[0].size(); cell++) {
            if (matrix[row][cell])
                board_layer += matrix[row][cell]->asAscii();
            else
                board_layer += '.';
        }
        board_layer += '\n';
    }

    // print piece layer
    for (unsigned row = 0; row < matrix.size(); row++) {
        for (unsigned cell = 0; cell < matrix[0].size(); cell++) {
            char appended_char = '.';

            if (active_piece) {
                // if there may be some piece minos (real or ghost) in this column
                if (active_piece_x <= static_cast<int>(cell)
                    && static_cast<int>(cell) <= active_piece_x + 3) {
                    // check ghost first - it should be under the real piece
                    if (ghost_piece_y <= row && row <= ghost_piece_y + 3u) {
                        const auto& mino = active_piece->currentGrid().at(row - ghost_piece_y)
                                                                      .at(cell - active_piece_x);
                        if (mino)
                            appended_char = 'g';
                    }
                    // check piece - overwrite the ascii char even if it has a value
                    if (active_piece_y <= row && row <= active_piece_y + 3u) {
                        const auto& mino = active_piece->currentGrid().at(row - active_piece_y)
                                                                      .at(cell - active_piece_x);
                        if (mino)
                            appended_char = std::tolower(mino->asAscii());
                    }
                }
            }

            piece_layer += appended_char;
        }
        piece_layer += '\n';
    }

    assert(board_layer.length() == piece_layer.length());
    std::string output;
    for (size_t i = 0; i < board_layer.length(); i++) {
        if (piece_layer.at(i) != '.') {
            output += piece_layer.at(i);
            continue;
        }

        output += board_layer.at(i);
    }
    return output;
}

#endif

void Well::draw(GraphicsContext& gcx, unsigned x, unsigned y)
{
    using Textures = GameplayResources::Textures;

    // Draw background
    for (size_t row = 0; row < 20; row++) {
        for (size_t col = 0; col < 10; col++) {
            gcx.drawTexture(Textures::MATRIXBG, {
                static_cast<int>(x + col * Mino::texture_size_px),
                static_cast<int>(y + row * Mino::texture_size_px),
                Mino::texture_size_px,
                Mino::texture_size_px
            });
        }
    }

    // Draw board Minos
    for (size_t row = 0; row < 20; row++) {
        for (size_t col = 0; col < 10; col++) {
            if (matrix.at(row + 2).at(col))
                matrix.at(row + 2).at(col)->draw(gcx,
                                             x + col * Mino::texture_size_px,
                                             y + row * Mino::texture_size_px);
        }
    }

    // Draw current piece
    if (active_piece) {
        // draw piece
        for (unsigned row = 0; row < 4; row++) {
            if (active_piece_y + row < 2) // hide rows 21-22
                continue;
            for (unsigned col = 0; col < 4; col++) {
                if (active_piece->currentGrid().at(row).at(col)) {
                    active_piece->currentGrid().at(row).at(col)
                    ->draw(gcx, x + (active_piece_x + col) * Mino::texture_size_px,
                                y + (active_piece_y + row - 2) * Mino::texture_size_px);
                }
            }
        }

        // draw ghost
        for (unsigned row = 0; row < 4; row++) {
            if (ghost_piece_y + row < 2) // hide rows 21-22
                continue;
            for (unsigned col = 0; col < 4; col++) {
                if (active_piece->currentGrid().at(row).at(col)) {
                    gcx.drawTexture(Textures::MINO_GHOST, {
                        static_cast<int>(x + (active_piece_x + col) * Mino::texture_size_px),
                        static_cast<int>(y + (ghost_piece_y + row - 2) * Mino::texture_size_px),
                        Mino::texture_size_px,
                        Mino::texture_size_px
                    });
                }
            }
        }
    }

    // Draw line clear animation
    if (pending_cleared_rows.size()) {
        for (auto row : pending_cleared_rows) {
            if (row < 2)
                continue;
            gcx.drawFilledRect({
                    static_cast<int>(x),
                    static_cast<int>(y + (row - 2) * Mino::texture_size_px),
                    static_cast<int>(Mino::texture_size_px * matrix.at(0).size()),
                    Mino::texture_size_px
                }, {0xFF, 0xFF, 0xFF, lineclear_alpha.value()});
        }
    }

    // Draw piece lock animation
    for (auto& anim : onlock_anims)
        anim.draw(gcx, x, y);
}

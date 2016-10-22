#include "LuaRotationFn.h"

#include "system/Log.h"

#include <exception>


const std::string LOG_TAG = "lua_rotation_plugin";

LuaRotationFn::LuaRotationFn(const std::string& scriptfile)
{
    lua.open_libraries(sol::lib::base, sol::lib::table);
    try {
        lua.script_file(scriptfile);
    }
    catch (const sol::error& err) {
        throw new std::runtime_error(err.what());
    }

    fn = lua["possibleRotations"];
    if (!fn.valid())
        throw new std::runtime_error("A rotation plugin must have a possibleRotations function");
}

std::vector<std::pair<int, int>> LuaRotationFn::call(PieceType piece, PieceDirection direction)
{
    std::vector<std::pair<int, int>> output;

    auto fn_call = fn(toAscii(piece), toAscii(direction));
    if (!fn_call.valid()) {
        sol::error err = fn_call;
        Log::error(LOG_TAG) << err.what() << '\n';
        return output;
    }

    sol::table arr = fn_call;
    for (unsigned i = 1; i <= arr.size(); i++) {
        sol::table pair = arr[i];
        if (pair.valid()) {
            if (pair.size() != 2) {
                Log::error(LOG_TAG) << "offsets must have 2 elements\n";
                continue;
            }
            sol::optional<int> x = pair[1];
            sol::optional<int> y = pair[2];
            if (x == sol::nullopt || y == sol::nullopt) {
                Log::error(LOG_TAG) << "offsets must contain integers\n";
                continue;
            }
            output.emplace_back(x.value(), y.value());
        }
    }

    return output;
}
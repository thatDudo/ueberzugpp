// Display images inside a terminal
// Copyright (C) 2023  JustKidding
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "hyprland.hpp"
#include "os.hpp"
#include "tmux.hpp"
#include "util/socket.hpp"

#include <filesystem>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <range/v3/all.hpp>

using njson = nlohmann::json;
namespace fs = std::filesystem;

HyprlandSocket::HyprlandSocket(const std::string_view signature)
    : logger(spdlog::get("wayland"))
{
    const auto socket_base_dir = os::getenv("XDG_RUNTIME_DIR").value_or("/tmp");
    const auto socket_rel_path = fmt::format("hypr/{}/.socket.sock", signature);
    socket_path = fmt::format("{}/{}", socket_base_dir, socket_rel_path);
    // XDG_RUNTIME_DIR set but hyprland < 0.40
    if (!fs::exists(socket_path)) {
        socket_path = fmt::format("/tmp/{}", socket_rel_path);
    }

    logger->info("Using hyprland socket {}", socket_path);
    const auto active = request_result("j/activewindow");
    address = active.at("address");
    set_active_monitor();
}

void HyprlandSocket::set_active_monitor()
{
    const auto monitors = request_result("j/monitors");
    for (const auto &monitor : monitors) {
        bool focused = monitor.at("focused");
        if (focused) {
            output_name = monitor.at("name");
            output_scale = monitor.at("scale");
            break;
        }
    }
}

auto HyprlandSocket::get_focused_output_name() -> std::string
{
    return output_name;
}

auto HyprlandSocket::request_result(const std::string_view payload) -> nlohmann::json
{
    const UnixSocket socket{socket_path};
    socket.write(payload.data(), payload.size());
    const std::string result = socket.read_until_empty();
    return njson::parse(result);
}

void HyprlandSocket::request(const std::string_view payload)
{
    const UnixSocket socket{socket_path};
    logger->debug("Running socket command {}", payload);
    socket.write(payload.data(), payload.length());
}

auto HyprlandSocket::get_active_window() -> nlohmann::json
{
    // recalculate address in case it changed
    if (tmux::is_used()) {
        const auto active = request_result("j/activewindow");
        address = active.at("address");
    }
    const auto clients = request_result("j/clients");
    const auto client = ranges::find_if(clients, [this](const njson &json) { return json.at("address") == address; });
    if (client == clients.end()) {
        throw std::runtime_error("Active window not found");
    }
    return *client;
}

auto HyprlandSocket::get_window_info() -> struct WaylandWindowGeometry {
    const auto terminal = get_active_window();
    const auto &sizes = terminal.at("size");
    const auto &coords = terminal.at("at");

    return {
        .width = sizes.at(0),
        .height = sizes.at(1),
        .x = coords.at(0),
        .y = coords.at(1),
    };
}

void HyprlandSocket::initial_setup(const std::string_view appid)
{
    disable_focus(appid);
    enable_floating(appid);
    remove_borders(appid);
    remove_rounding(appid);
}

void HyprlandSocket::remove_rounding(const std::string_view appid)
{
    const auto payload = fmt::format("/keyword windowrulev2 rounding 0,title:{}", appid);
    request(payload);
}

void HyprlandSocket::disable_focus(const std::string_view appid)
{
    const auto payload = fmt::format("/keyword windowrulev2 nofocus,title:{}", appid);
    request(payload);
}

void HyprlandSocket::enable_floating(const std::string_view appid)
{
    const auto payload = fmt::format("/keyword windowrulev2 float,title:{}", appid);
    request(payload);
}

void HyprlandSocket::remove_borders(const std::string_view appid)
{
    const auto payload = fmt::format("/keyword windowrulev2 noborder,title:{}", appid);
    request(payload);
}

void HyprlandSocket::change_workspace(const std::string_view appid, int workspaceid)
{
    const auto payload = fmt::format("/dispatch movetoworkspacesilent {},title:{}", workspaceid, appid);
    request(payload);
}

void HyprlandSocket::move_window(const std::string_view appid, int xcoord, int ycoord)
{
    auto terminal = get_active_window();
    const auto &workspace = terminal.at("workspace");
    const int workspaceid = workspace.at("id");
    change_workspace(appid, workspaceid);
    
    int res_x = xcoord;
    int res_y = ycoord;
    if (output_scale > 1.0F) {
        const int offset = 10;
        res_x = res_x / 2 + offset;
        res_y = res_y / 2 + offset;
    }
    const auto payload = fmt::format("/dispatch movewindowpixel exact {} {},title:{}", res_x, res_y, appid);
    request(payload);
}

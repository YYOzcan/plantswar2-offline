// In-process game server (port of server/*.py), reached through virtual sockets.
#pragma once
#include <string>

void server_init(const std::string &save_path);
// feed a raw HTTP request received on the given local port; returns false until the request is complete
bool server_http(int port, const std::string &request, std::string &response);
// test hook: handle one action.php payload (plaintext)
std::string server_replay(const std::string &plain);

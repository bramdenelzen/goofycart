#pragma once
// The master server's HTTP API, mounted on a cpp-httplib server.
//
//   POST /v1/accounts            {"name"}                 -> {"id","name","token","rating"}
//   GET  /v1/me                  (Bearer token)           -> account
//   GET  /v1/servers?protocol=N                           -> {"servers":[...]}
//   GET  /v1/match?region=R&protocol=N                    -> server, or 404
//   POST /v1/servers/heartbeat   (X-Server-Key) server    -> {}
//   POST /v1/sessions/verify     (X-Server-Key) {"token"} -> account, or 404
//   POST /v1/results             (X-Server-Key) {"accounts":[ids in finishing order]} -> {"ratings":{id:rating}}
//   GET  /healthz
#include "gk/online/master.h"

#include <functional>
#include <string>

namespace httplib {
class Server;
}

namespace gk::online {

// `server_key` guards the game-server routes; empty allows any caller (local
// testing). `clock` returns seconds.
void install_master_routes(httplib::Server& http, MasterState& state, std::string server_key,
                           std::function<double()> clock);

} // namespace gk::online

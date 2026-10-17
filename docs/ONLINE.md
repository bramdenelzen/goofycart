# Running Gobby Kart online

Three programs work together:

| Program | Protocol | Job |
| --- | --- | --- |
| `gk_master` | HTTPS (TCP) | Accounts, the list of game servers, matchmaking, ratings |
| `gk_server` | UDP (ENet) | One race room: the authoritative simulation for up to 12 karts |
| `gk_client` | both | The game: ONLINE signs in and matchmakes through the master, then races on a game server |

A LAN party needs only `gk_server` and `gk_client --connect host`. The
master is for public play.

## The master server

```sh
gk_master --port 7780 --data /var/lib/gobbykart/accounts.json \
          --server-key "$GK_SERVER_KEY" \
          --cert /etc/letsencrypt/live/master.example.com/fullchain.pem \
          --key  /etc/letsencrypt/live/master.example.com/privkey.pem
```

- **TLS.** With `--cert` and `--key` it serves HTTPS itself (builds with
  OpenSSL 3). Without them it serves plain HTTP; put it behind a TLS proxy
  then, for example Caddy: `master.example.com { reverse_proxy 127.0.0.1:7780 }`.
  Note that behind a proxy every game server appears to come from the proxy's
  address, so give each game server `--public-host`.
- **Server key.** Game servers prove themselves with a shared secret
  (`--server-key` or `GK_SERVER_KEY` on both sides). Without one anybody can
  list servers and post results, which is fine on a LAN only.
- **Accounts.** A player's first ONLINE makes an account from their name
  (`--name`, made unique) and keeps a random token in their user data folder
  (`account.txt`). The master stores only a SHA-256 hash of each token, in the
  `--data` JSON file, written after every change.
- **Ratings.** Each race with at least two signed-in players updates their
  ratings: Elo between every pair, the better-placed player winning each
  pair, K = 32 shared across the field. CPU karts do not count.

### API

| Route | Who | Body / answer |
| --- | --- | --- |
| `POST /v1/accounts` | player | `{"name"}` → `{"id","name","token","rating"}` |
| `GET /v1/me` | player (`Authorization: Bearer token`) | account |
| `GET /v1/servers?protocol=N` | anyone | `{"servers":[...]}` |
| `GET /v1/match?region=R&protocol=N` | player | best server, or 404 |
| `POST /v1/servers/heartbeat` | game server (`X-Server-Key`) | `{"port","name","region","protocol","players","max_players","phase","track"[,"host"]}` |
| `POST /v1/sessions/verify` | game server | `{"token"}` → account, or 404 |
| `POST /v1/results` | game server | `{"accounts":[ids in finishing order]}` → new ratings |
| `GET /healthz` | anyone | `{"ok":true}` |

Matchmaking prefers the player's region, then a server whose lobby is open
(so you race next instead of watching), then the busiest one, never a full
one. Servers that miss heartbeats for 30 seconds drop off the list.

## Game servers

```sh
gk_server --port 7777 --master https://master.example.com --server-key "$GK_SERVER_KEY" \
          --region eu --name "Gobby EU 1" [--public-host eu1.example.com]
```

The server heartbeats every 10 seconds with its phase, track and player
count, asks the master who each player is when they join with a token (they
then race under their account name), and reports each race's signed-in
players in finishing order. All of that runs on a separate thread, so a slow
or missing master never stalls the 60 Hz simulation; players without an
account (or with a master that is down) race as guests.

## Players

ONLINE on the title menu needs the master's address once: `--master URL`,
or `master_url=` in `settings.txt`. `--region eu` picks the region to look
in first. `GK_MASTER_CA=ca.pem` trusts a private certificate authority, for
a self-signed master.

## Testing it on one machine

```sh
gk_master --port 7780 --data /tmp/accounts.json &
gk_server --master http://127.0.0.1:7780 --region eu &
gk_botclient --master http://127.0.0.1:7780 --region eu --clients 3   # sign up, matchmake, race
gk_client --master http://127.0.0.1:7780 --name Albert                # then ONLINE
```

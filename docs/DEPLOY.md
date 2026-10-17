# Deploying Gobby Kart servers

A deployment is one **master server** (accounts, matchmaking, ratings, crash
reports; HTTPS) and any number of **race servers** (one 12-seat room per UDP
port) in one or more regions. Players only need the master's address.

```
players ──HTTPS──> master.example.com (Caddy -> gk_master)
   │                     ^ heartbeats, sign-in checks, results (HTTPS + server key)
   └──UDP──> eu1.example.com:7777..   na1.example.com:7777..   (gk_server)
```

Everything here is in [`deploy/`](../deploy). The server image is built from
the repository (`deploy/Dockerfile`: the game servers, master and test bots,
no client), and the same programs are in the Linux release tarball for
running under systemd instead.

## Sizing

A race server is one thread running the 60 Hz simulation for 12 karts plus
a snapshot stream to each player, so one core runs several rooms; give each
room its own port. Measure with a soak run before sizing a fleet (see
"Checking a deployment"). The master does very little work
(a request per player per race, a heartbeat per room every 10 s) and keeps
everything in one JSON file and a `crashes/` folder.

## 1. The master (once)

On a host with a DNS name, TCP 80 and 443 open, and Docker:

```sh
git clone https://github.com/Mexury/gobbykart && cd gobbykart/deploy/master
cp example.env .env      # set GK_MASTER_DOMAIN and a GK_SERVER_KEY (openssl rand -hex 24)
docker compose up -d --build
curl https://master.example.com/healthz   # {"ok":true}
```

Caddy gets and renews the certificate. Accounts and crash reports are in the
`master-data` volume; back it up (`docker compose cp master:/var/lib/gobbykart .`).

## 2. Race servers (per region)

On each region host, with the UDP ports open:

```sh
cd gobbykart/deploy/region
cp example.env .env      # GK_MASTER_URL, the same GK_SERVER_KEY, GK_REGION, GK_PUBLIC_HOST
docker compose up -d --build
docker compose logs -f   # "master: registered as Gobby eu 1 (eu)"
```

Add rooms by copying a service in `compose.yml` with the next port. Check
from anywhere with `curl https://master.example.com/v1/servers`.

## Without Docker (systemd)

Unpack the Linux release tarball to `/opt/gobbykart`, then:

```sh
sudo cp deploy/systemd/*.service /etc/systemd/system/
sudo mkdir -p /etc/gobbykart
echo "GK_SERVER_KEY=..." | sudo tee /etc/gobbykart/master.env          # master host
sudo tee /etc/gobbykart/server.env <<'X'                              # region hosts
GK_MASTER_URL=https://master.example.com
GK_SERVER_KEY=...
GK_REGION=eu
GK_PUBLIC_HOST=eu1.example.com
X
sudo systemctl enable --now gk-master                 # master host, behind a TLS proxy
sudo systemctl enable --now gk-server@7777 gk-server@7778
```

## Pointing the game at it

Players start the game once with `--master https://master.example.com`
(or set `master_url=` in `settings.txt`), and optionally `--region eu`.
To ship a build that knows its master, set `master_url` in the default
settings you distribute.

## Upgrading

Race servers and the game must speak the same protocol version; matchmaking
only offers servers with the player's version, so upgrade the race servers
when a release changes it and old and new players are each matched to
their own. The master is compatible across versions.

## Checking a deployment

`gk_botclient --master https://master.example.com --region eu --clients 3
--seconds 180 --until-results` signs up three bots, matchmakes, races one
race and exits; their ratings then show up as changed.

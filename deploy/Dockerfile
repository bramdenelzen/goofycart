# Game server, master server and test bots in one small image (no client,
# GPU or audio). Build from the repository root:
#
#   docker build -f deploy/Dockerfile -t gobbykart .
#
# then run gk_server or gk_master (see deploy/master and deploy/region).
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      cmake ninja-build g++ git ca-certificates libssl-dev python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake --preset server -DGK_BUILD_TESTS=OFF \
    && cmake --build --preset server --target gk_server gk_master gk_botclient \
    && strip build/server/src/server/gk_server build/server/src/online/gk_master build/server/src/server/gk_botclient

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libssl3t64 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home-dir /var/lib/gobbykart --create-home gobby
COPY --from=build /src/build/server/src/server/gk_server /src/build/server/src/online/gk_master \
                  /src/build/server/src/server/gk_botclient /opt/gobbykart/
COPY data /opt/gobbykart/data
ENV PATH=/opt/gobbykart:$PATH
USER gobby
# Accounts, crash reports (master) and the server's own crash reports live here.
WORKDIR /var/lib/gobbykart
VOLUME /var/lib/gobbykart
EXPOSE 7777/udp 7780/tcp
CMD ["gk_server"]

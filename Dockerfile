# build
FROM ubuntu:22.04 AS build

RUN apt-get update && apt-get install -y \
  build-essential cmake pkg-config \
  libssl-dev \
  libjsoncpp-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN make

# run
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
  libssl3 \
  libjsoncpp25 \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /app/bin/map_veto_server /app/server

ENV PORT=8080
EXPOSE 8080

CMD ["/app/server"]

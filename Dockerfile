# Stage 1: Build
FROM ubuntu:latest AS builder

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN mkdir -p build \
 && cd build \
 && cmake .. -DCMAKE_BUILD_TYPE=Release \
 && cmake --build . -- -j"$(nproc)"


# Stage 2: Runtime
FROM ubuntu:latest

# install Python + the distro-packaged requests + C++ runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libstdc++6 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# copy just what we need
COPY --from=builder /app/build/bin/service ./service

EXPOSE 1555

ENTRYPOINT ["sh", "-c"]

CMD ["true"]

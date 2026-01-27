# Multi-stage Dockerfile for building PostFiat on Ubuntu 24.04
#
# Build arguments for configuration selection:
#   NETWORK: mainnet, testnet, devnet (default: devnet)
#   NODE_SIZE: full, light (default: full)
#
# Examples:
#   Build with default (devnet-full):
#     docker build -t postfiatd .
#
#   Build with mainnet-full:
#     docker build --build-arg NETWORK=mainnet --build-arg NODE_SIZE=full -t postfiatd:mainnet-full .
#
#   Build with testnet-light:
#     docker build --build-arg NETWORK=testnet --build-arg NODE_SIZE=light -t postfiatd:testnet-light .
#
FROM ubuntu:24.04 AS builder

# Build arguments for configuration selection
ARG NETWORK=devnet
ARG NODE_SIZE=light

# Avoid interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# Set working directory
WORKDIR /postfiat

# Install build dependencies + logrotate and cron for log management
RUN apt update

RUN apt install --yes curl git libssl-dev pipx python3.12-dev python3-pip make gcc-13 g++-13 libprotobuf-dev protobuf-compiler logrotate cron

# Set GCC 13 as default compiler
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

RUN curl --location --remote-name \
  "https://github.com/Kitware/CMake/releases/download/v3.25.1/cmake-3.25.1.tar.gz"
RUN tar -xzf cmake-3.25.1.tar.gz
RUN rm cmake-3.25.1.tar.gz
WORKDIR /postfiat/cmake-3.25.1
RUN ./bootstrap --parallel=$(nproc)
RUN make --jobs $(nproc)
RUN make install
WORKDIR /postfiat

RUN pipx install conan
RUN pipx ensurepath
RUN export PATH=$PATH:/root/.local/bin
ENV PATH="/root/.local/bin:$PATH"

# Copy source code first (needed for conan profile setup)
COPY . .

RUN conan profile detect --force
RUN conan config install conan/profiles/ -tf $(conan config home)/profiles/
RUN conan remote add --index 0 xrplf https://conan.ripplex.io

#Build
WORKDIR /postfiat/.build

RUN conan install .. --output-folder . --build missing --settings build_type=Debug
RUN cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -Dxrpld=ON -Dtests=ON -Dvalidator_keys=ON ..
RUN cmake --build . -j $(nproc)
RUN cmake --build . --target validator-keys -j $(nproc)

# run tests
# RUN ./postfiatd --unittest

# CLEANUP SECTION - Remove build artifacts and unnecessary files
RUN rm -rf /postfiat/.build/build \
    && rm -rf /postfiat/.build/CMakeFiles \
    && rm -rf /postfiat/.build/CMakeCache.txt \
    && rm -rf /postfiat/.build/*.cmake \
    && rm -rf /postfiat/.build/Makefile \
    && rm -rf /postfiat/.build/compile_commands.json \
    && rm -rf /postfiat/.build/conan* \
    && rm -rf /root/.conan \
    && rm -rf /root/.local/share/conan

# Create directories
RUN mkdir -p /var/lib/postfiatd/db /var/log/postfiatd /etc/postfiatd

# Copy the built binaries from builder stage
RUN cp /postfiat/.build/postfiatd /usr/local/bin/postfiatd
RUN cp /postfiat/.build/validator-keys/validator-keys /usr/local/bin/validator-keys

# Copy configuration files based on build arguments
# Construct the config filename from NETWORK and NODE_SIZE
RUN cp /postfiat/cfg/postfiatd-${NETWORK}-${NODE_SIZE}.cfg /etc/postfiatd/postfiatd.cfg
RUN cp /postfiat/cfg/validators-${NETWORK}.txt /etc/postfiatd/validators.txt

# Setup log rotation
RUN cp /postfiat/scripts/logrotate-postfiatd.conf /etc/logrotate.d/postfiatd
# Add cron job to run logrotate every hour (with state file in persistent volume)
RUN echo "0 * * * * root /usr/sbin/logrotate -s /var/log/postfiatd/.logrotate.status /etc/logrotate.d/postfiatd" > /etc/cron.d/postfiatd-logrotate
RUN chmod 0644 /etc/cron.d/postfiatd-logrotate

# Copy and setup entrypoint script
RUN cp /postfiat/scripts/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

# Set working directory
WORKDIR /var/lib/postfiatd

# Declare volumes
VOLUME ["/etc/postfiatd"]
VOLUME ["/var/lib/postfiatd/db"]
VOLUME ["/var/log/postfiatd"]

# Expose ports (based on the config file)
EXPOSE 5005 2559 6005 6006 50051

# Set the entrypoint (uses wrapper script that starts cron for log rotation)
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]

# Default command arguments
CMD ["--conf", "/etc/postfiatd/postfiatd.cfg"]

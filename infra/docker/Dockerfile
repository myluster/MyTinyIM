# 使用 Ubuntu 22.04 作为基础镜像
FROM ubuntu:22.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Replace with Aliyun mirrors for faster apt downloads in China
RUN sed -i 's@//.*archive.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list \
    && sed -i 's@//.*security.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list

# 1. Update sources and install build tools

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    gdb \
    curl \
    wget \
    zip \
    unzip \
    pkg-config \
    autoconf \
    libtool \
    libssl-dev \
    ca-certificates \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 2. 安装 Boost 库
RUN apt-get update && apt-get install -y \
    libboost-all-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 3. 安装 gRPC 和 Protobuf
RUN apt-get update && apt-get install -y \
    protobuf-compiler \
    libprotobuf-dev \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 4. 安装数据库客户端库
RUN apt-get update && apt-get install -y \
    libmysqlclient-dev \
    default-mysql-client \
    libhiredis-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 5. 安装日志库和 JSON 库
RUN apt-get update && apt-get install -y \
    libspdlog-dev \
    nlohmann-json3-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 6. 安装并编译 Google Test (GTest)
RUN apt-get update && apt-get install -y libgtest-dev \
    && cd /usr/src/gtest \
    && cmake . \
    && make \
    && cp lib/libgtest*.a /usr/lib \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

CMD ["/bin/bash"]

FROM ubuntu:22.04

# Prevent interactive prompts during apt install
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies (ADDED python-is-python3 HERE)
RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    wget \
    make \
    libncurses-dev \
    flex \
    bison \
    gperf \
    python3 \
    python3-pip \
    python3-venv \
    python3-serial \
    python-is-python3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory for ESP tools
WORKDIR /opt/esp

# Download and extract the Xtensa LX106 Toolchain
RUN wget -q https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz && \
    tar -xzf xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz && \
    rm xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz

# Clone the ESP8266 RTOS SDK
RUN git clone --recursive https://github.com/espressif/ESP8266_RTOS_SDK.git

# Set environment variables so the toolchain and SDK are always available
ENV IDF_PATH=/opt/esp/ESP8266_RTOS_SDK
ENV PATH="/opt/esp/xtensa-lx106-elf/bin:${PATH}"

# Pin setuptools and install SDK python requirements
RUN python3 -m pip install --no-cache-dir "setuptools<82" && \
    python3 -m pip install --no-cache-dir -r ${IDF_PATH}/requirements.txt

WORKDIR /workspace

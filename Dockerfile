# Use --platform=linux/amd64 when building on Apple Silicon Macs
FROM ubuntu:24.04

ENV TZ=Europe/Budapest
ENV DEBIAN_FRONTEND=noninteractive

# 1) Install basic tools needed by the Arduino CLI installer
RUN apt-get update && apt-get install -y \
    ca-certificates \
    curl \
    unzip \
    xz-utils \
    wget \
  && rm -rf /var/lib/apt/lists/*

# 2) Install Arduino CLI using the official install script
#    This script installs the latest Arduino CLI into /usr/local/bin
#    Ref: official Arduino CLI installation docs: https://arduino.github.io/arduino-cli/latest/installation/
#    Use current latest release version which is v1.3.1 as of Nov 2025
RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/v1.3.1/install.sh \
    | BINDIR=/usr/local/bin sh

# 3) Initialize Arduino CLI config (creates ~/.arduino15/arduino-cli.yaml)
#    Add additional configurations
ARG ARDUINO_SILABS_CONFIG="https://siliconlabs.github.io/arduino/package_arduinosilabs_index.json"

RUN arduino-cli config init \
    && arduino-cli config set board_manager.additional_urls $ARDUINO_SILABS_CONFIG \
    && arduino-cli config set network.connection_timeout 600s \
    && arduino-cli core update-index

# 4) Update core index and install a core for AVR based Arduinos and Silicon Labs based boards
RUN arduino-cli core install arduino:avr \
    && arduino-cli core install SiliconLabs:silabs

# 5) Install Simple FOC library
# Note: Currently simpleFOC2.3.5 doesn't support our boards, so we have to use a workaround, but from 2.3.6 it should be fine
# Note: Use the latest dev version as of Nov 2025 which is "395b6cd4c621fe460c1d61a99bc0fc38913d5481"

RUN mkdir -p /root/Arduino/libraries \
&& wget -O /tmp/Arduino-FOC-395b6cd4c621fe460c1d61a99bc0fc38913d5481.zip https://github.com/simplefoc/Arduino-FOC/archive/395b6cd4c621fe460c1d61a99bc0fc38913d5481.zip \
&& unzip /tmp/Arduino-FOC-395b6cd4c621fe460c1d61a99bc0fc38913d5481.zip -d /tmp \
&& mv /tmp/Arduino-FOC-395b6cd4c621fe460c1d61a99bc0fc38913d5481 /root/Arduino/libraries/Arduino-FOC \
&& rm /tmp/Arduino-FOC-395b6cd4c621fe460c1d61a99bc0fc38913d5481.zip

# Note: Use the line below instead of the command above to install the official release version from the library manager
#RUN arduino-cli lib install "Simple FOC"

# 5) Set a working directory where you will mount your sketches
WORKDIR /workspace

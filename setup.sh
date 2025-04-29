#!/bin/bash

# brew install openssl

# Update package list
sudo apt update

# Install necessary dependencies
sudo apt install -y build-essential libssl-dev git cmake ninja-build

# Install and setup CMocka

# IF debian based
if [ -x "$(command -v apt-get)" ]; then
    sudo apt-get install -y cmocka-dev
else if [ -x "$(command -v brew)" ]; then
    brew install cmocka
fi

# Clone and build liboqs if not installed
if [ ! -d "liboqs" ]; then
    git clone --branch main https://github.com/open-quantum-safe/liboqs.git
    cd liboqs
    mkdir build && cd build
    cmake -GNinja -DCMAKE_INSTALL_PREFIX=../install ..
    ninja
    ninja install
    cd ../..
fi

echo "Setup complete. You can now run 'make' to build the blockchain."

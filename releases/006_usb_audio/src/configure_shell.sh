#!/bin/bash

# Define the paths to add
# These match the values found in the previous setup_env.sh
PICO_TOOLCHAIN_PATH="/Users/.../.pico-sdk/toolchain/14_2_Rel1/bin"
PICO_CMAKE_PATH="/Users/.../.pico-sdk/cmake/v3.31.5/CMake.app/Contents/bin"
PICO_NINJA_PATH="/Users/.../.pico-sdk/ninja/v1.12.1"
PICO_SDK_PATH="/Users/.../.pico-sdk/sdk/2.2.0"

# Check if .zshrc exists
RC_FILE="$HOME/.zshrc"
if [ ! -f "$RC_FILE" ]; then
    echo "Creating $RC_FILE..."
    touch "$RC_FILE"
fi

# Append to .zshrc if not already present
grep -q "PICO_SDK_PATH" "$RC_FILE"
if [ $? -ne 0 ]; then
    echo "" >> "$RC_FILE"
    echo "# Pico SDK configuration" >> "$RC_FILE"
    echo "export PICO_SDK_PATH=$PICO_SDK_PATH" >> "$RC_FILE"
    echo "export PATH=$PICO_CMAKE_PATH:$PICO_NINJA_PATH:$PICO_TOOLCHAIN_PATH:\$PATH" >> "$RC_FILE"
    echo "Added Pico SDK configuration to $RC_FILE"
else
    echo "Pico SDK configuration appears to be already present in $RC_FILE. Skipping."
fi

echo "Please run 'source $RC_FILE' or restart your terminal to apply changes."

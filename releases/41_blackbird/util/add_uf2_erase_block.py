#!/usr/bin/env python3
"""
Add flash erase blocks to a UF2 file to clear user script storage area.
This ensures that flashing a UF2 clears any stored user scripts.
"""

import struct
import sys

UF2_MAGIC_START0 = 0x0A324655  # "UF2\n"
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30

# User script storage area (last 16KB of 2MB flash)
USER_SCRIPT_OFFSET = (2 * 1024 * 1024) - (16 * 1024)
USER_SCRIPT_SIZE = 16 * 1024
BLOCK_SIZE = 256  # UF2 block size

def create_erase_block(target_addr, block_num, total_blocks):
    """Create a UF2 block filled with 0xFF (erased flash)"""
    block = bytearray(512)
    
    # UF2 header
    struct.pack_into("<I", block, 0, UF2_MAGIC_START0)
    struct.pack_into("<I", block, 4, UF2_MAGIC_START1)
    struct.pack_into("<I", block, 8, 0x2000)  # familyID present flag
    struct.pack_into("<I", block, 12, target_addr)
    struct.pack_into("<I", block, 16, BLOCK_SIZE)
    struct.pack_into("<I", block, 20, block_num)
    struct.pack_into("<I", block, 24, total_blocks)
    struct.pack_into("<I", block, 28, 0xe48bff56)  # RP2040 family ID
    
    # Data: 256 bytes of 0xFF (erased flash)
    for i in range(BLOCK_SIZE):
        block[32 + i] = 0xFF
    
    # Magic end
    struct.pack_into("<I", block, 512 - 4, UF2_MAGIC_END)
    
    return bytes(block)

def modify_uf2(input_path, output_path):
    """Add erase blocks for user script area to UF2 file"""
    
    with open(input_path, 'rb') as f:
        original_data = f.read()
    
    # Parse original UF2 to count blocks
    original_blocks = len(original_data) // 512
    
    # Calculate how many erase blocks we need (16KB / 256 bytes per block)
    erase_blocks_needed = USER_SCRIPT_SIZE // BLOCK_SIZE
    total_blocks = original_blocks + erase_blocks_needed
    
    # Read original blocks and update total count
    blocks = []
    for i in range(original_blocks):
        block = bytearray(original_data[i*512:(i+1)*512])
        # Update total blocks count
        struct.pack_into("<I", block, 24, total_blocks)
        blocks.append(bytes(block))
    
    # Add erase blocks for user script area
    for i in range(erase_blocks_needed):
        target_addr = 0x10000000 + USER_SCRIPT_OFFSET + (i * BLOCK_SIZE)
        block_num = original_blocks + i
        erase_block = create_erase_block(target_addr, block_num, total_blocks)
        blocks.append(erase_block)
    
    # Write modified UF2
    with open(output_path, 'wb') as f:
        for block in blocks:
            f.write(block)
    
    print(f"Added {erase_blocks_needed} erase blocks to UF2")
    print(f"Original: {original_blocks} blocks, Modified: {total_blocks} blocks")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.uf2> <output.uf2>")
        sys.exit(1)
    
    modify_uf2(sys.argv[1], sys.argv[2])

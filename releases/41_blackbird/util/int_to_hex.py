#!/usr/bin/env python3
"""Convert space-separated integers to space-separated hex bytes or ASCII string."""

import sys

def int_to_hex(input_string):
    """Convert space-separated integers to space-separated hex bytes.
    
    Args:
        input_string: String containing space-separated integers
        
    Returns:
        String containing space-separated hex bytes (e.g., "1A 2B")
    """
    try:
        integers = [int(x) for x in input_string.split()]
        hex_bytes = [f"{x:02X}" for x in integers]
        return " ".join(hex_bytes)
    except ValueError as e:
        return f"Error: Invalid integer in input - {e}"

def int_to_ascii(input_string):
    """Convert space-separated integers to ASCII/UTF-8 string.
    
    Args:
        input_string: String containing space-separated integers
        
    Returns:
        UTF-8 decoded string
    """
    try:
        integers = [int(x) for x in input_string.split()]
        byte_array = bytes(integers)
        return byte_array.decode('utf-8')
    except ValueError as e:
        return f"Error: Invalid integer in input - {e}"
    except UnicodeDecodeError as e:
        return f"Error: Cannot decode as UTF-8 - {e}"

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python int_to_hex.py [-ascii] \"1 2 3 255\"")
        print("Example: python int_to_hex.py \"10 20 30 255\"")
        print("Example: python int_to_hex.py -ascii \"72 101 108 108 111\"")
        sys.exit(1)
    
    ascii_mode = False
    if sys.argv[1] == "-ascii":
        ascii_mode = True
        input_str = " ".join(sys.argv[2:])
    else:
        input_str = " ".join(sys.argv[1:])
    
    if ascii_mode:
        result = int_to_ascii(input_str)
    else:
        result = int_to_hex(input_str)
    
    print(result)

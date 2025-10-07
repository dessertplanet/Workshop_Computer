#!/usr/bin/env python3
"""
lua2header.py - Convert Lua files to C header files with embedded bytecode
Similar to crow's build process but using Python for better CMake integration
"""

import os
import sys
import subprocess
import tempfile
from pathlib import Path

def compile_lua_to_bytecode(lua_file):
    """Compile Lua file to bytecode using luac, fallback to source embedding"""
    # Try our host-built luac first (with compatible settings)
    script_dir = Path(__file__).parent
    host_luac = script_dir.parent / 'build' / 'host_luac' / 'luac'
    build_luac = script_dir.parent / 'build' / 'lua' / 'src' / 'luac'
    
    luac_candidates = [
        str(host_luac),  # Our compatible host luac (first priority)
        str(build_luac)
    ]
    
    for luac_cmd in luac_candidates:
        try:
            with tempfile.NamedTemporaryFile(suffix='.lc', delete=False) as tmp:
                try:
                    subprocess.run([luac_cmd, '-o', tmp.name, lua_file], 
                                 check=True, capture_output=True)
                    with open(tmp.name, 'rb') as f:
                        print(f"Successfully compiled with {luac_cmd}")
                        return f.read()
                finally:
                    if os.path.exists(tmp.name):
                        os.unlink(tmp.name)
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    
    # If all luac attempts fail, fall back to embedding source as compiled bytecode manually
    print(f"Warning: luac not found, embedding Lua source as string literal")
    with open(lua_file, 'rb') as f:
        lua_source = f.read()
    
    # Create a simple Lua bytecode header manually (this is a fallback)
    # We'll just embed the source and use luaL_dostring instead of luaL_loadbuffer
    return lua_source

def bytecode_to_c_array(bytecode, var_name):
    """Convert bytecode to C array format"""
    lines = []
    lines.append(f"const unsigned char {var_name}[] = {{")
    
    # Format as hex bytes, 12 per line
    for i in range(0, len(bytecode), 12):
        chunk = bytecode[i:i+12]
        hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f'  {hex_bytes}{"," if i + 12 < len(bytecode) else ""}')
    
    lines.append("};")
    lines.append(f"const unsigned int {var_name}_len = {len(bytecode)};")
    
    return '\n'.join(lines)

def generate_variable_name(lua_file):
    """Generate C variable name from Lua filename"""
    name = Path(lua_file).stem  # Get filename without extension
    # Replace non-alphanumeric with underscore and make valid C identifier
    name = ''.join(c if c.isalnum() else '_' for c in name)
    if name[0].isdigit():
        name = '_' + name
    return name

def main():
    if len(sys.argv) != 3:
        print("Usage: lua2header.py <input.lua> <output.h>")
        sys.exit(1)
    
    lua_file = sys.argv[1]
    header_file = sys.argv[2]
    
    if not os.path.exists(lua_file):
        print(f"Error: Lua file '{lua_file}' not found")
        sys.exit(1)
    
    try:
        # Compile Lua to bytecode
        print(f"Compiling {lua_file} to bytecode...")
        bytecode = compile_lua_to_bytecode(lua_file)
        
        # Generate variable name
        var_name = generate_variable_name(lua_file)
        
        # Generate C header content
        header_content = f"""#pragma once

// Generated from {os.path.basename(lua_file)}
// Do not edit manually

{bytecode_to_c_array(bytecode, var_name)}
"""
        
        # Ensure output directory exists
        os.makedirs(os.path.dirname(header_file), exist_ok=True)
        
        # Write header file
        with open(header_file, 'w') as f:
            f.write(header_content)
        
        print(f"Generated {header_file} (variable: {var_name}, size: {len(bytecode)} bytes)")
        
    except subprocess.CalledProcessError as e:
        print(f"Error compiling Lua file: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()

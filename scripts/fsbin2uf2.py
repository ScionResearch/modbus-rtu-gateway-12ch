import os
import struct
import shutil
from SCons.Script import AlwaysBuild

Import("env")

# Helper to parse sizes like "640K", "2M", "123456"
def parse_size(size_str):
    size_str = size_str.strip().upper()
    if size_str.endswith("K"):
        return int(size_str[:-1]) * 1024
    elif size_str.endswith("M"):
        return int(size_str[:-1]) * 1024 * 1024
    else:
        return int(size_str)

# UF2 conversion function for filesystem binary
def create_uf2(data, base_address=0x10000000, family_id=0xE48BFF56):
    """Convert binary data to UF2 format for RP2040"""
    UF2_MAGIC_START0 = 0x0A324655
    UF2_MAGIC_START1 = 0x9E5D5157
    UF2_MAGIC_END = 0x0AB16F30
    UF2_FLAG_FAMILY_ID_PRESENT = 0x2000
    
    block_size = 256
    blocks = []
    total_blocks = (len(data) + block_size - 1) // block_size
    
    block_num = 0
    for i in range(0, len(data), block_size):
        chunk = data[i:i + block_size]
        if len(chunk) < block_size:
            chunk += b'\x00' * (block_size - len(chunk))
        
        block = struct.pack('<LLLLLLLL',
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_FLAG_FAMILY_ID_PRESENT,
            base_address + i,  # Target address in flash
            block_size,
            block_num,         # Block sequence number
            total_blocks,      # Total number of blocks
            family_id
        )
        block += chunk
        block += b'\x00' * (476 - len(chunk))  # Pad to 476 bytes
        block += struct.pack('<L', UF2_MAGIC_END)
        blocks.append(block)
        block_num += 1
    
    return b''.join(blocks)

# Read config values
fs_size_str = env.GetProjectOption("board_build.filesystem_size")
flash_size_str = env.GetProjectOption("board_build.upload_flash_size")

if not fs_size_str:
    raise ValueError("board_build.filesystem_size is not set in platformio.ini")
if not flash_size_str:
    raise ValueError("board_build.upload_flash_size is not set in platformio.ini")

fs_size = parse_size(fs_size_str)
flash_size = parse_size(flash_size_str)

# Calculate filesystem offset using PlatformIO's logic
# RP2040: FS starts at flash_size - fs_size - eeprom_size
eeprom_size = 4096  # 4KB EEPROM space
fs_offset = flash_size - fs_size - eeprom_size

# Paths
fs_path = os.path.join(env.subst("$BUILD_DIR"), "littlefs.bin")
fs_uf2_path = os.path.join(env.subst("$BUILD_DIR"), "filesystem.uf2")
fw_uf2_path = os.path.join(env.subst("$BUILD_DIR"), "firmware.uf2")
project_root = env.subst("$PROJECT_DIR")
uf2_output_dir = os.path.join(project_root, "uf2Files")

def create_filesystem_uf2(source, target, env):
    """Create filesystem.uf2 from littlefs.bin"""
    print(f"Creating filesystem UF2 from {fs_path}")
    
    # Check if littlefs.bin exists
    if not os.path.exists(fs_path):
        print(f"Error: {fs_path} not found. Build the filesystem first.")
        return
    
    # Read filesystem binary
    with open(fs_path, "rb") as f:
        fs_data = f.read()
    
    print(f"Filesystem size: {len(fs_data)} bytes")
    
    # Calculate filesystem base address
    fs_base_address = 0x10000000 + fs_offset
    
    # Create filesystem UF2 at correct offset
    fs_uf2_data = create_uf2(fs_data, base_address=fs_base_address)
    
    with open(fs_uf2_path, "wb") as f:
        f.write(fs_uf2_data)
    
    # Calculate blocks
    fs_blocks = (len(fs_data) + 255) // 256
    
    print(f"Filesystem UF2 created:")
    print(f"  Input: {fs_path} ({len(fs_data):,} bytes)")
    print(f"  Output: {fs_uf2_path} ({len(fs_uf2_data):,} bytes, {fs_blocks} blocks)")
    print(f"  Flash address: 0x{fs_base_address:08X}")
    print(f"  Filesystem offset: 0x{fs_offset:08X}")
    
    # Create uf2Files directory if it doesn't exist
    os.makedirs(uf2_output_dir, exist_ok=True)
    
    # Copy filesystem.uf2 to project uf2Files folder
    fs_dest = os.path.join(uf2_output_dir, "filesystem.uf2")
    shutil.copy2(fs_uf2_path, fs_dest)
    print(f"  Copied to: {fs_dest}")
    
    # Copy firmware.uf2 if it exists
    if os.path.exists(fw_uf2_path):
        fw_dest = os.path.join(uf2_output_dir, "firmware.uf2")
        shutil.copy2(fw_uf2_path, fw_dest)
        print(f"  Copied firmware.uf2 to: {fw_dest}")
    else:
        print(f"  Warning: {fw_uf2_path} not found - only filesystem.uf2 copied")
    
    print(f"\nUF2 files ready for deployment in: {uf2_output_dir}")

# Add custom build target
filesystem_target = env.Command(
    fs_uf2_path,
    fs_path,
    create_filesystem_uf2
)

AlwaysBuild(filesystem_target)
env.Alias("filesystem", filesystem_target)

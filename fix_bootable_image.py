import os
import sys

# Size of `boot` and `recovery` partitions
PARTITION_SIZE = 64 * 1024 * 1024   # 64 MB
BOOTIMG_MAGIC = b'ANDROID!'

SIGNERVER2_MAGIC = b'SignerVer02'
SIGNERVER2_SIZE = 512

SEANDROID_ENFORCE = b'SEANDROIDENFORCE'

# AVB footer
AVB_FOOTER_MAGIC = b'AVBf'
AVB_FOOTER_SIZE = 64
AVB_FOOTER_VERSION_MAJOR = 1
AVB_FOOTER_VERSION_MINOR = 0

def generate_avb_footer(original_image_size: int):
    footer = (
        AVB_FOOTER_MAGIC +
        AVB_FOOTER_VERSION_MAJOR.to_bytes(4, 'big') +
        AVB_FOOTER_VERSION_MINOR.to_bytes(4, 'big') +
        original_image_size.to_bytes(8, 'big')
    )

    return footer + b'\x00' * (AVB_FOOTER_SIZE - len(footer))

def exit(msg):
    print('Error: ' + msg)
    sys.exit(1)

if len(sys.argv) != 3:
    exit('Usage: python fix_bootable_image.py <boot/recovery image> <output image>')
if not os.path.isfile(sys.argv[1]):
    exit(f'File not found: {sys.argv[1]}')
if os.path.isfile(sys.argv[2]):
    exit(f'File already exists: {sys.argv[2]}')

with open(sys.argv[1], 'rb') as f:
    data = bytearray(f.read())

if data[:len(BOOTIMG_MAGIC)] != BOOTIMG_MAGIC:
    exit('Not an Android bootable image')

if SIGNERVER2_MAGIC in data:
    exit(f'SignerVer02 magic already present. The image doesn\'t require fixing')

# Check if AVB footer is already present
if data[-AVB_FOOTER_SIZE:].startswith(AVB_FOOTER_MAGIC):
    print('Adding magic & modifying AVB footer...')

    # Read existing AVB footer
    pos = len(data) - AVB_FOOTER_SIZE
    pos += len(AVB_FOOTER_MAGIC)  # Skip magic
    version_major = int.from_bytes(data[pos:pos + 4], 'big')
    pos += 4
    version_minor = int.from_bytes(data[pos:pos + 4], 'big')
    pos += 4
    original_image_size = int.from_bytes(data[pos:pos + 8], 'big')
    if version_major != AVB_FOOTER_VERSION_MAJOR or version_minor != AVB_FOOTER_VERSION_MINOR:
        print(f'Warning: Unexpected AVB footer version: {version_major}.{version_minor}')

    # If SEANDROIDENFORCE is present, preserve it
    if data[original_image_size:original_image_size + len(SEANDROID_ENFORCE)] == SEANDROID_ENFORCE:
        original_image_size += len(SEANDROID_ENFORCE)
        print(f'Preserving {SEANDROID_ENFORCE.decode()}')

    # Add SignerVer02 magic
    assert data[original_image_size:original_image_size + SIGNERVER2_SIZE] == b'\x00' * SIGNERVER2_SIZE
    data[original_image_size:original_image_size + len(SIGNERVER2_MAGIC)] = SIGNERVER2_MAGIC

    # Update AVB footer
    original_image_size += SIGNERVER2_SIZE
    data[pos:pos + 8] = original_image_size.to_bytes(8, 'big')

else:   # AVB footer not present
    print('Adding magic & AVB footer...')

    new_img_size = len(data) + SIGNERVER2_SIZE + AVB_FOOTER_SIZE
    if new_img_size >= PARTITION_SIZE:
        # Lets check if the image ends with empty bytes so we can remove them
        to_be_removed = new_img_size - PARTITION_SIZE
        assert data[-to_be_removed:] == b'\x00' * to_be_removed
        data = data[:-to_be_removed]

    # Add SignerVer02 magic
    data += SIGNERVER2_MAGIC
    data += b'\x00' * (SIGNERVER2_SIZE - len(SIGNERVER2_MAGIC))

    # Add AVB footer
    original_image_size = len(data)
    data += b'\x00' * (PARTITION_SIZE - len(data) - AVB_FOOTER_SIZE)
    data += generate_avb_footer(original_image_size)

if len(data) != PARTITION_SIZE:
    exit(f'Fixed image\'s size does not match the partition size')

with open(sys.argv[2], 'wb') as f:
    f.write(data)

print(f'Success! Fixed image saved to "{sys.argv[2]}"')

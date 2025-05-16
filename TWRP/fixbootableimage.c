#include <endian.h> 
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARTITION_SIZE 64 * 1024 * 1024 // 64MB

#define BOOT_MAGIC "ANDROID!"
#define BOOT_MAGIC_SIZE 8
#define BOOT_NAME_SIZE 16
#define BOOT_ARGS_SIZE 512
#define BOOT_EXTRA_ARGS_SIZE 1024

#define SIGNERVER2_MAGIC "SignerVer02"
#define SIGNERVER2_MAGIC_SIZE 11
#define SIGNERVER2_SIZE 512

#define AVB_FOOTER_MAGIC "AVBf"
#define AVB_FOOTER_MAGIC_LEN 4
#define AVB_FOOTER_SIZE 64
#define AVB_FOOTER_VERSION_MAJOR 1
#define AVB_FOOTER_VERSION_MINOR 0

struct boot_img_hdr_v1
{
    uint8_t magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;               /* size in bytes */
    uint32_t kernel_addr;               /* physical load addr */
    uint32_t ramdisk_size;              /* size in bytes */
    uint32_t ramdisk_addr;              /* physical load addr */

    uint32_t second_size;               /* size in bytes */
    uint32_t second_addr;               /* physical load addr */

    uint32_t tags_addr;                 /* physical addr for kernel tags */
    uint32_t page_size;                 /* flash page size we assume */
    uint32_t header_version;
    uint32_t os_version;
    uint8_t name[BOOT_NAME_SIZE];       /* asciiz product name */
    uint8_t cmdline[BOOT_ARGS_SIZE];
    uint32_t id[8];                     /* timestamp / checksum / sha1 / etc */
    uint8_t extra_cmdline[BOOT_EXTRA_ARGS_SIZE];

    uint32_t recovery_dtbo_size;    /* size of recovery image */
    uint64_t recovery_dtbo_offset;  /* offset in boot image */
    uint32_t header_size;               /* size of boot image header in bytes */
} __attribute__((packed));

struct AvbFooter {
    /*   0: Four bytes equal to "AVBf" (AVB_FOOTER_MAGIC). */
    uint8_t magic[AVB_FOOTER_MAGIC_LEN];
    /*   4: The major version of the footer struct. */
    uint32_t version_major;
    /*   8: The minor version of the footer struct. */
    uint32_t version_minor;
    /*  12: The original size of the image on the partition. */
    uint64_t original_image_size;
    /*  20: The offset of the |AvbVBMetaImageHeader| struct. */
    uint64_t vbmeta_offset;
    /*  28: The size of the vbmeta block (header + auth + aux blocks). */
    uint64_t vbmeta_size;
    /*  36: Padding to ensure struct is size AVB_FOOTER_SIZE bytes. This
     * must be set to zeroes.
     */
    uint8_t reserved[28];
} __attribute__((packed));

int main(int argc, char ** argv) {
    if (argc != 2) {
        printf("Usage: %s <boot.img>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb+");
    if (!f) {
        perror("fopen failed");
        return 2;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 0 || file_size > PARTITION_SIZE) {
        printf("Invalid image size: %ld\n", file_size);
        fclose(f);
        return 3;
    }

    // Check if this is a valid boot image
    struct boot_img_hdr_v1 header;
    size_t read_size = fread(&header, sizeof(header), 1, f);
    if (read_size != 1) {
        perror("fread failed for header");
        fclose(f);
        return 4;
    }
    if (memcmp(header.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0) {
        printf("Not a valid boot image\n");
        fclose(f);
        return 5;
    }
    if (header.header_version != 1) {
        printf("Unexpected boot image version: %u\n", header.header_version);
        fclose(f);
        return 6;
    }

    // Add the AVB Footer if not present and ensure it points to the SignerVer02 Magic
    fseek(f, -AVB_FOOTER_SIZE, SEEK_END);
    struct AvbFooter footer;
    size_t footer_read_size = fread(&footer, sizeof(footer), 1, f);
    if (footer_read_size != 1) {
        perror("fread failed for AVB footer");
        fclose(f);
        return 7;
    }

    if (memcmp(footer.magic, AVB_FOOTER_MAGIC, AVB_FOOTER_MAGIC_LEN) != 0) {
        printf("Adding magic & AVB footer\n");

        if (file_size > PARTITION_SIZE - AVB_FOOTER_SIZE - SIGNERVER2_SIZE) {
            printf("Not enough space in image\n");
            fclose(f);
            return 8;
        }

        // Add SignerVer02 magic at the end
        fseek(f, 0, SEEK_END);
        uint8_t magic[SIGNERVER2_SIZE] = SIGNERVER2_MAGIC;
        size_t written_size = fwrite(magic, sizeof(magic), 1, f);
        if (written_size != 1) {
            perror("fwrite failed for SignerVer02 magic");
            fclose(f);
            return 9;
        }

        file_size += SIGNERVER2_SIZE;

        // Add AVB footer
        struct AvbFooter new_footer = {
            .magic = AVB_FOOTER_MAGIC,
            .version_major = htobe32(AVB_FOOTER_VERSION_MAJOR),
            .version_minor = htobe32(AVB_FOOTER_VERSION_MINOR),
            .original_image_size = htobe64(file_size),
            .vbmeta_offset = 0,
            .vbmeta_size = 0,
            .reserved = {0}
        };

        fseek(f, PARTITION_SIZE - AVB_FOOTER_SIZE, SEEK_SET);
        written_size = fwrite(&new_footer, sizeof(new_footer), 1, f);
        if (written_size != 1) {
            perror("fwrite failed for AVB footer");
            fclose(f);
            return 10;
        }

        fclose(f);
        return 0;
    }

    if (file_size != PARTITION_SIZE) {
        printf("Found AVB footer but the image doesn't match the size of the partition\n");
        fclose(f);
        return 11;
    }

    printf("Found AVB footer\n");
    footer.version_major = be32toh(footer.version_major);
    footer.version_minor = be32toh(footer.version_minor);
    footer.original_image_size = be64toh(footer.original_image_size);

    if (footer.version_major != AVB_FOOTER_VERSION_MAJOR ||
        footer.version_minor != AVB_FOOTER_VERSION_MINOR) {
        printf("Unexpected AVB footer version: %u.%u\n",
               footer.version_major, footer.version_minor);
        fclose(f);
        return 12;
    }

    fseek(f, footer.original_image_size - SIGNERVER2_SIZE, SEEK_SET);
    uint8_t magic[SIGNERVER2_MAGIC_SIZE];
    read_size = fread(magic, sizeof(magic), 1, f);
    if (read_size != 1) {
        perror("fread failed for SignerVer02 magic");
        fclose(f);
        return 13;
    }
    if (memcmp(magic, SIGNERVER2_MAGIC, SIGNERVER2_MAGIC_SIZE) == 0) {
        printf("SignerVer02 magic already present, nothing to do\n");
        fclose(f);
        return 14;
    }

    printf("Adding magic & modifying AVB footer\n");
    fseek(f, footer.original_image_size, SEEK_SET);
    uint8_t data[SIGNERVER2_SIZE];
    read_size = fread(data, sizeof(data), 1, f);
    if (read_size != 1) {
        perror("fread failed for post-image end data");
        fclose(f);
        return 15;
    }
    for (size_t i = 0; i < SIGNERVER2_SIZE; i++) {
        if (data[i] != 0) {
            printf("Unexpected data at end of image\n");
            fclose(f);
            return 16;
        }
    }

    fseek(f, footer.original_image_size, SEEK_SET);
    size_t written_size = fwrite(SIGNERVER2_MAGIC, sizeof(SIGNERVER2_MAGIC), 1, f);
    if (written_size != 1) {
        perror("fwrite failed for SignerVer02 magic");
        fclose(f);
        return 17;
    }
    footer.original_image_size += SIGNERVER2_SIZE;

    footer.version_major = htobe32(footer.version_major);
    footer.version_minor = htobe32(footer.version_minor);
    footer.original_image_size = htobe64(footer.original_image_size);
    fseek(f, PARTITION_SIZE - AVB_FOOTER_SIZE, SEEK_SET);
    written_size = fwrite(&footer, sizeof(footer), 1, f);
    if (written_size != 1) {
        perror("fwrite failed for AVB footer");
        fclose(f);
        return 18;
    }

    fclose(f);
    return 0;
}
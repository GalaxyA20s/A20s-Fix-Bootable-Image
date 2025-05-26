#include <endian.h> 
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

void abortf(int code, const char *fmt, ...) {
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
    }

    // if (errno) perror("Last error");
    exit(code);
}

int main(int argc, char ** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    if (argc != 2) 
        abortf(1, "Usage: %s <boot.img>", argv[0]);

    FILE *f = fopen(argv[1], "rb+");
    if (!f) abortf(2, "fopen failed");

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 0 || file_size > PARTITION_SIZE) 
        abortf(3, "Invalid image size: %ld", file_size);

    // Check if this is a valid boot image
    struct boot_img_hdr_v1 header;
    if (fread(&header, sizeof(header), 1, f) != 1)
        abortf(4, "fread failed for header");
    if (memcmp(header.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0)
        abortf(5, "Not a valid boot image");
    if (header.header_version != 1)
        abortf(6, "Unexpected boot image version: %u", header.header_version);

    struct AvbFooter footer;

    fseek(f, -AVB_FOOTER_SIZE, SEEK_END);
    if (fread(&footer, AVB_FOOTER_SIZE, 1, f) != 1)
        abortf(7, "fread failed for AVB footer");
    if (memcmp(footer.magic, AVB_FOOTER_MAGIC, AVB_FOOTER_MAGIC_LEN) != 0) {
        // AVB footer not present, lets add it
        printf("Adding magic & AVB footer\n");

        long new_img_size = file_size + SIGNERVER2_SIZE + AVB_FOOTER_SIZE;
        if (new_img_size > PARTITION_SIZE) {
            // If the image ends with empty bytes we can remove them

            long to_be_removed = new_img_size - PARTITION_SIZE;
            fseek(f, -to_be_removed, SEEK_END);
            uint8_t last_bytes[to_be_removed];
            if (fread(last_bytes, sizeof(last_bytes), 1, f) != 1)
                abortf(8, "fread failed for last_bytes");

            for (long i = 0; i < to_be_removed; i++) {
                if (last_bytes[i] != 0)
                    abortf(9, "Not enough space in image");
            }    

            file_size -= to_be_removed;
            if (ftruncate(fileno(f), file_size) != 0)
                abortf(10, "ftruncate failed");
        }

        // Add SignerVer02 magic at the end
        fseek(f, 0, SEEK_END);
        uint8_t magic[SIGNERVER2_SIZE] = SIGNERVER2_MAGIC;
        if (fwrite(magic, sizeof(magic), 1, f) != 1)
            abortf(11, "fwrite failed for SignerVer02 magic");

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
        if (fwrite(&new_footer, sizeof(new_footer), 1, f) != 1)
            abortf(12, "fwrite failed for AVB footer");
    } else {
        // AVB footer present, lets ensure it points to the SignerVer02 Magic
        printf("Found AVB footer\n");
        int version_major = be32toh(footer.version_major);
        int version_minor = be32toh(footer.version_minor);
        footer.original_image_size = be64toh(footer.original_image_size);
        if (version_major != AVB_FOOTER_VERSION_MAJOR || version_minor != AVB_FOOTER_VERSION_MINOR)
            abortf(13, "Unexpected AVB footer version: %u.%u", version_major, version_minor);

        if (file_size != PARTITION_SIZE)
            abortf(14, "Image size doesn't match partition size");

        fseek(f, footer.original_image_size - SIGNERVER2_SIZE, SEEK_SET);
        uint8_t magic[SIGNERVER2_MAGIC_SIZE];
        if (fread(magic, sizeof(magic), 1, f) != 1)
            abortf(15, "fread failed for SignerVer02 magic");
        if (memcmp(magic, SIGNERVER2_MAGIC, SIGNERVER2_MAGIC_SIZE) == 0)
            abortf(16, "SignerVer02 magic already present, nothing to do");

        printf("Adding magic & modifying AVB footer\n");
        if (file_size - AVB_FOOTER_SIZE - footer.original_image_size < SIGNERVER2_SIZE)
            abortf(17, "Not enough space between boot image & AVB footer");

        fseek(f, footer.original_image_size, SEEK_SET);
        uint8_t zeroes[SIGNERVER2_SIZE];
        memset(zeroes, 0, sizeof(zeroes));
        if (fwrite(zeroes, sizeof(zeroes), 1, f) != 1)
            abortf(18, "fwrite failed for zeroes");
        fseek(f, footer.original_image_size, SEEK_SET);
        if (fwrite(SIGNERVER2_MAGIC, sizeof(SIGNERVER2_MAGIC), 1, f) != 1)
            abortf(19, "fwrite failed for SignerVer02 magic");
        footer.original_image_size += SIGNERVER2_SIZE;

        footer.original_image_size = htobe64(footer.original_image_size);
        fseek(f, PARTITION_SIZE - AVB_FOOTER_SIZE, SEEK_SET);
        if (fwrite(&footer, sizeof(footer), 1, f) != 1)
            abortf(20, "fwrite failed for AVB footer");
    }

    fseek(f, 0, SEEK_END);
    long new_file_size = ftell(f);
    if (new_file_size != PARTITION_SIZE)
        abortf(21, "Fixed image's size doesn't match the partition size");

    fclose(f);
    return 0;
}
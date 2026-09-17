/* Run the exact firmware import-preflight implementation against actual
 * experimental ELFs in CI. This does not relocate or execute ELF code. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "private/esp_privileged_imports.h"

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    for (int i = 1; i < argc; ++i) {
        FILE *file = fopen(argv[i], "rb");
        if (!file) return 2;
        if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 2; }
        long size = ftell(file);
        if (size <= 0 || size > 8L * 1024 * 1024 ||
            fseek(file, 0, SEEK_SET) != 0) { fclose(file); return 2; }
        uint8_t *image = (uint8_t *)malloc((size_t)size);
        if (!image) { fclose(file); return 2; }
        size_t read_bytes = fread(image, 1, (size_t)size, file);
        int io_error = ferror(file);
        fclose(file);
        const int accepted = !io_error && read_bytes == (size_t)size &&
            esp_elf_privileged_imports_valid_v1(image, (size_t)size);
        free(image);
        if (!accepted) {
            fprintf(stderr, "Private privileged ELF import preflight REJECTED: %s\n", argv[i]);
            return 1;
        }
        printf("Private privileged ELF import preflight PASS: %s\n", argv[i]);
    }
    return 0;
}

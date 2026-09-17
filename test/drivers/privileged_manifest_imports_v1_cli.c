/* Execute the EXACT firmware C import checks against actual linked Xtensa ELF
 * bytes and a canonical sidecar produced by the build, not a fixture.
 * This checks consistency only; a sidecar is NOT authenticated by itself. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "private/esp_privileged_imports.h"
#include "private/esp_privileged_manifest_imports.h"

#define IMAGE_LIMIT (8u * 1024u * 1024u)
#define DECL_LIMIT (128u * 128u)
#define IMPORT_LIMIT 128u

static uint8_t *read_file(const char *path, size_t limit, size_t *size)
{
    FILE *stream = fopen(path, "rb");
    if (!stream) return NULL;
    if (fseek(stream, 0, SEEK_END) != 0) { fclose(stream); return NULL; }
    long end = ftell(stream);
    if (end <= 0 || (unsigned long)end > limit || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)end + 1u);
    if (!bytes) { fclose(stream); return NULL; }
    if (fread(bytes, 1, (size_t)end, stream) != (size_t)end ||
        fgetc(stream) != EOF || fclose(stream) != 0) {
        free(bytes);
        return NULL;
    }
    bytes[end] = 0;
    *size = (size_t)end;
    return bytes;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s verified-candidate.elf privileged-imports.v1\n", argv[0]);
        return 2;
    }
    size_t image_size = 0, declaration_size = 0;
    uint8_t *image = read_file(argv[1], IMAGE_LIMIT, &image_size);
    uint8_t *declaration = read_file(argv[2], DECL_LIMIT, &declaration_size);
    if (!image || !declaration) {
        fprintf(stderr, "Cannot read bounded ELF and declaration\n");
        free(image);
        free(declaration);
        return 1;
    }
    const char *names[IMPORT_LIMIT] = {0};
    size_t count = 0;
    bool canonical = declaration[declaration_size - 1] == '\n';
    size_t start = 0;
    for (size_t i = 0; canonical && i < declaration_size; ++i) {
        if (declaration[i] == '\n') {
            const size_t len = i - start;
            if (!len || len > 127 || count == IMPORT_LIMIT) { canonical = false; break; }
            for (size_t j = start; j < i; ++j) {
                if (declaration[j] <= 0x20 || declaration[j] > 0x7e) canonical = false;
            }
            if (!canonical) break;
            declaration[i] = '\0';
            names[count++] = (const char *)declaration + start;
            start = i + 1;
        }
    }
    const bool exact = canonical && count && start == declaration_size &&
        esp_elf_privileged_imports_valid_v1(image, image_size) &&
        esp_elf_privileged_manifest_imports_match_v1(image, image_size, names, count);
    if (!exact) fprintf(stderr, "Rejected: invalid ABI or declaration mismatch: %s\n", argv[1]);
    else printf("Physical ELF exact private import-set match PASS (%zu imports): %s\n", count, argv[1]);
    free(image);
    free(declaration);
    return exact ? 0 : 1;
}

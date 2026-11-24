/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * A minimal user of amark: includes the public header, links Amark::amark,
 * checks that the library's version matches the header's, and parses a short
 * document and reads the source bytes of its root with amark_node_raw.
 */

#include <amark/amark.h>

#include <stdio.h>
#include <string.h>

/*
 * Parses a short document and checks that the source bytes of its root are
 * the whole content. Returns 1 on success, 0 (after a message) on failure.
 */
static int check_raw(void)
{
    static const char content[] = "# Title\n\nSome *text*.\n";
    amark_document_t *doc = NULL;
    amark_error_t error;
    const char *bytes = NULL;
    size_t length = 0;
    int ok;

    error = amark_parse(content, sizeof(content) - 1, NULL, &doc);
    if (error != AMARK_ERROR_NONE || doc == NULL) {
        fprintf(stderr, "amark_parse() failed: %d\n", (int)error);
        return 0;
    }
    ok = amark_node_raw(doc, amark_document_root(doc), &bytes, &length)
        && length == sizeof(content) - 1
        && bytes != NULL
        && memcmp(bytes, content, length) == 0;
    amark_document_destroy(doc);
    if (!ok) {
        fprintf(stderr, "amark_node_raw() of the root is not the content\n");
        return 0;
    }
    return 1;
}

int main(void)
{
    const char *library_version = amark_version_string();

    if (library_version == NULL) {
        fprintf(stderr, "amark_version_string() returned NULL\n");
        return 1;
    }
    if (strcmp(library_version, AMARK_VERSION_STRING) != 0) {
        fprintf(stderr, "version mismatch: library \"%s\", header \"%s\"\n",
            library_version, AMARK_VERSION_STRING);
        return 1;
    }
    if (!check_raw()) {
        return 1;
    }
    printf("Amark %s\n", library_version);
    return 0;
}

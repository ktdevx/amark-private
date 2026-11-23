/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * A minimal user of amark: includes the public header, links Amark::amark and
 * checks that the library's version matches the header's.
 */

#include <amark/amark.h>

#include <stdio.h>
#include <string.h>

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
    printf("amark %s\n", library_version);
    return 0;
}

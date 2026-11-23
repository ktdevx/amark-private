/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "scan.h"

/* The only white space of the block structure: the space and the tab. */
static bool is_space_or_tab(unsigned char c)
{
    return c == ' ' || c == '\t';
}

/* Returns the first offset from i that is not a space or a tab, or len. */
static size_t skip_space_or_tab(const unsigned char *p, size_t i, size_t len)
{
    while (i < len && is_space_or_tab(p[i])) {
        i++;
    }
    return i;
}

/*
 * Returns the offset right after the last byte before end that is not a
 * space or a tab, but not less than start.
 */
static size_t trim_space_or_tab(const unsigned char *p, size_t start,
                                size_t end)
{
    while (end > start && is_space_or_tab(p[end - 1])) {
        end--;
    }
    return end;
}

/* Returns the length of the run of c that starts at p. */
static size_t run_length(const unsigned char *p, size_t len, unsigned char c)
{
    size_t i = 0;

    while (i < len && p[i] == c) {
        i++;
    }
    return i;
}

bool amark_scan_thematic_break(const unsigned char *p, size_t len)
{
    unsigned char c;
    size_t count = 0;
    size_t i;

    if (len == 0) {
        return false;
    }
    c = p[0];
    if (c != '-' && c != '_' && c != '*') {
        return false;
    }
    for (i = 0; i < len; i++) {
        if (p[i] == c) {
            count++;
        } else if (!is_space_or_tab(p[i])) {
            return false;
        }
    }
    return count >= 3;
}

bool amark_scan_atx_heading(const unsigned char *p, size_t len,
                            unsigned *level, size_t *content_start,
                            size_t *content_end)
{
    size_t hashes = run_length(p, len, '#');
    size_t start;
    size_t end;
    size_t closing;

    if (hashes == 0 || hashes > 6) {
        return false;
    }
    if (hashes < len && !is_space_or_tab(p[hashes])) {
        return false;
    }
    start = skip_space_or_tab(p, hashes, len);
    end = trim_space_or_tab(p, start, len);

    /*
     * An optional closing sequence: a run of '#' at the end, preceded by a
     * space or a tab, or making up the whole content.
     */
    closing = end;
    while (closing > start && p[closing - 1] == '#') {
        closing--;
    }
    if (closing == start) {
        end = start;
    } else if (closing < end && is_space_or_tab(p[closing - 1])) {
        end = trim_space_or_tab(p, start, closing);
    }

    *level = (unsigned)hashes;
    *content_start = start;
    *content_end = end;
    return true;
}

unsigned amark_scan_setext_underline(const unsigned char *p, size_t len)
{
    size_t run;

    if (len == 0 || (p[0] != '=' && p[0] != '-')) {
        return 0;
    }
    run = run_length(p, len, p[0]);
    if (skip_space_or_tab(p, run, len) != len) {
        return 0;
    }
    return p[0] == '=' ? 1u : 2u;
}

bool amark_scan_open_fence(const unsigned char *p, size_t len,
                           char *fence_char, size_t *fence_length,
                           size_t *info_start, size_t *info_end)
{
    size_t run;
    size_t start;
    size_t end;
    size_t i;

    if (len == 0 || (p[0] != '`' && p[0] != '~')) {
        return false;
    }
    run = run_length(p, len, p[0]);
    if (run < 3) {
        return false;
    }
    if (p[0] == '`') {
        for (i = run; i < len; i++) {
            if (p[i] == '`') {
                return false;
            }
        }
    }
    start = skip_space_or_tab(p, run, len);
    end = trim_space_or_tab(p, start, len);

    *fence_char = (char)p[0];
    *fence_length = run;
    *info_start = start;
    *info_end = end;
    return true;
}

size_t amark_scan_close_fence(const unsigned char *p, size_t len,
                              char fence_char)
{
    size_t run = run_length(p, len, (unsigned char)fence_char);

    if (run < 3 || skip_space_or_tab(p, run, len) != len) {
        return 0;
    }
    return run;
}

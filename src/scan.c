/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "scan.h"

#include <string.h>

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

bool amark_scan_list_marker(const unsigned char *p, size_t len,
                            amark_list_marker_t *out)
{
    amark_list_type_t type;
    uint32_t number = 0;
    size_t width;

    if (len == 0) {
        return false;
    }
    if (p[0] == '-' || p[0] == '+' || p[0] == '*') {
        type = AMARK_LIST_BULLET;
        width = 1;
    } else {
        /* At most 9 digits, so number stays below 10^9 and fits. */
        width = 0;
        while (width < len && p[width] >= '0' && p[width] <= '9') {
            if (width == 9) {
                return false;
            }
            number = number * 10u + (uint32_t)(p[width] - '0');
            width++;
        }
        if (width == 0 || width == len ||
            (p[width] != '.' && p[width] != ')')) {
            return false;
        }
        type = AMARK_LIST_ORDERED;
        width++;
    }
    if (width < len && !is_space_or_tab(p[width])) {
        return false;
    }

    out->type = type;
    out->marker = (char)p[width - 1];
    out->start_number = number;
    out->width = width;
    out->blank_after = skip_space_or_tab(p, width, len) == len;
    return true;
}

/* ---- HTML blocks -------------------------------------------------------- */

static bool is_ascii_letter(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_ascii_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static unsigned char ascii_lower(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

/*
 * Returns true when the bytes at p start with the n bytes of s, compared
 * without regard to ASCII case. s is lowercase.
 */
static bool starts_with_ci(const unsigned char *p, size_t len, const char *s,
                           size_t n)
{
    size_t i;

    if (len < n) {
        return false;
    }
    for (i = 0; i < n; i++) {
        if (ascii_lower(p[i]) != (unsigned char)s[i]) {
            return false;
        }
    }
    return true;
}

/* Returns true when the bytes at p start with the n bytes of s exactly. */
static bool starts_with(const unsigned char *p, size_t len, const char *s,
                        size_t n)
{
    return len >= n && memcmp(p, s, n) == 0;
}

/*
 * Returns true when the n bytes of s occur in the line. n is a small
 * constant, so the search is linear in len.
 */
static bool contains(const unsigned char *p, size_t len, const char *s,
                     size_t n)
{
    size_t i;

    for (i = 0; i + n <= len; i++) {
        if (memcmp(p + i, s, n) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * The tag names of start condition 1, lowercase. No name is a prefix of
 * another, so at most one of them starts a given line.
 */
static const char raw_text_names[4][9] = {
    "pre", "script", "style", "textarea"
};

/*
 * Returns the length of the name of start condition 1 that the bytes at p
 * start with, compared without regard to ASCII case, or 0 when they start
 * with none of them. What follows the name is not checked.
 */
static size_t raw_text_name_length(const unsigned char *p, size_t len)
{
    size_t i;

    for (i = 0; i < sizeof(raw_text_names) / sizeof(raw_text_names[0]);
         i++) {
        size_t n = strlen(raw_text_names[i]);
        if (starts_with_ci(p, len, raw_text_names[i], n)) {
            return n;
        }
    }
    return 0;
}

/*
 * The tag names of start condition 6, lowercase and sorted in byte order
 * for a binary search. The longest name has 10 bytes.
 */
#define BLOCK_TAG_NAME_MAX 10
static const char block_tag_names[][BLOCK_TAG_NAME_MAX + 1] = {
    "address", "article", "aside", "base", "basefont", "blockquote", "body",
    "caption", "center", "col", "colgroup", "dd", "details", "dialog",
    "dir", "div", "dl", "dt", "fieldset", "figcaption", "figure", "footer",
    "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head",
    "header", "hr", "html", "iframe", "legend", "li", "link", "main",
    "menu", "menuitem", "nav", "noframes", "ol", "optgroup", "option", "p",
    "param", "search", "section", "summary", "table", "tbody", "td",
    "tfoot", "th", "thead", "title", "tr", "track", "ul"
};

/* Returns true when name, lowercase and NUL-terminated, is a block tag. */
static bool is_block_tag_name(const char *name)
{
    size_t low = 0;
    size_t high = sizeof(block_tag_names) / sizeof(block_tag_names[0]);

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int order = strcmp(name, block_tag_names[mid]);
        if (order == 0) {
            return true;
        }
        if (order < 0) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }
    return false;
}

/* Start condition 1. p[0] is '<'. */
static bool html_start_1(const unsigned char *p, size_t len)
{
    size_t n = raw_text_name_length(p + 1, len - 1);

    if (n == 0) {
        return false;
    }
    n++;
    return n == len || is_space_or_tab(p[n]) || p[n] == '>';
}

/* Start condition 6. p[0] is '<'. */
static bool html_start_6(const unsigned char *p, size_t len)
{
    char name[BLOCK_TAG_NAME_MAX + 1];
    size_t i = 1;
    size_t n = 0;

    if (i < len && p[i] == '/') {
        i++;
    }
    while (i < len && (is_ascii_letter(p[i]) || is_ascii_digit(p[i]))) {
        if (n == BLOCK_TAG_NAME_MAX) {
            return false;
        }
        name[n++] = (char)ascii_lower(p[i]);
        i++;
    }
    if (n == 0) {
        return false;
    }
    name[n] = '\0';
    if (i < len && !is_space_or_tab(p[i]) && p[i] != '>' &&
        !(p[i] == '/' && i + 1 < len && p[i + 1] == '>')) {
        return false;
    }
    return is_block_tag_name(name);
}

/*
 * Returns the offset after the tag name that starts at i, or i when no tag
 * name starts there. A tag name is an ASCII letter followed by ASCII
 * letters, digits and '-'.
 */
static size_t scan_tag_name(const unsigned char *p, size_t i, size_t len)
{
    if (i == len || !is_ascii_letter(p[i])) {
        return i;
    }
    i++;
    while (i < len &&
           (is_ascii_letter(p[i]) || is_ascii_digit(p[i]) || p[i] == '-')) {
        i++;
    }
    return i;
}

/*
 * Returns the offset after the attribute name that starts at i, or i when
 * none starts there: an ASCII letter, '_' or ':', followed by ASCII letters,
 * digits, '_', '.', ':' and '-'.
 */
static size_t scan_attribute_name(const unsigned char *p, size_t i,
                                  size_t len)
{
    if (i == len ||
        !(is_ascii_letter(p[i]) || p[i] == '_' || p[i] == ':')) {
        return i;
    }
    i++;
    while (i < len && (is_ascii_letter(p[i]) || is_ascii_digit(p[i]) ||
                       p[i] == '_' || p[i] == '.' || p[i] == ':' ||
                       p[i] == '-')) {
        i++;
    }
    return i;
}

/*
 * Returns the offset after the attribute value that starts at i, or i when
 * none starts there: a quoted string, or a nonempty run of bytes other than
 * white space, '"', '\'', '=', '<', '>' and '`'.
 */
static size_t scan_attribute_value(const unsigned char *p, size_t i,
                                   size_t len)
{
    size_t j;

    if (i == len) {
        return i;
    }
    if (p[i] == '"' || p[i] == '\'') {
        for (j = i + 1; j < len; j++) {
            if (p[j] == p[i]) {
                return j + 1;
            }
        }
        return i;
    }
    j = i;
    while (j < len && !is_space_or_tab(p[j]) && p[j] != '"' &&
           p[j] != '\'' && p[j] != '=' && p[j] != '<' && p[j] != '>' &&
           p[j] != '`') {
        j++;
    }
    return j;
}

/*
 * Returns the offset after the open tag that starts at p[0] == '<', or 0
 * when no complete open tag starts there or its name is one of start
 * condition 1.
 */
static size_t scan_open_tag(const unsigned char *p, size_t len)
{
    size_t i = scan_tag_name(p, 1, len);

    if (i == 1) {
        return 0;
    }
    if (raw_text_name_length(p + 1, i - 1) == i - 1) {
        return 0;
    }
    for (;;) {
        size_t space = skip_space_or_tab(p, i, len);
        size_t name_end;
        size_t eq;
        size_t value;

        if (space == i) {
            break;
        }
        name_end = scan_attribute_name(p, space, len);
        if (name_end == space) {
            i = space;
            break;
        }
        i = name_end;
        eq = skip_space_or_tab(p, name_end, len);
        if (eq < len && p[eq] == '=') {
            eq = skip_space_or_tab(p, eq + 1, len);
            value = scan_attribute_value(p, eq, len);
            if (value == eq) {
                return 0;
            }
            i = value;
        }
    }
    if (i < len && p[i] == '/') {
        i++;
    }
    if (i == len || p[i] != '>') {
        return 0;
    }
    return i + 1;
}

/*
 * Returns the offset after the closing tag that starts at p[0] == '<', or 0
 * when no complete closing tag starts there.
 */
static size_t scan_closing_tag(const unsigned char *p, size_t len)
{
    size_t i;

    if (len < 2 || p[1] != '/') {
        return 0;
    }
    i = scan_tag_name(p, 2, len);
    if (i == 2) {
        return 0;
    }
    i = skip_space_or_tab(p, i, len);
    if (i == len || p[i] != '>') {
        return 0;
    }
    return i + 1;
}

/* Start condition 7. p[0] is '<'. */
static bool html_start_7(const unsigned char *p, size_t len)
{
    size_t end = scan_open_tag(p, len);

    if (end == 0) {
        end = scan_closing_tag(p, len);
    }
    return end != 0 && skip_space_or_tab(p, end, len) == len;
}

unsigned amark_scan_html_start(const unsigned char *p, size_t len)
{
    if (len < 2 || p[0] != '<') {
        return 0;
    }
    if (html_start_1(p, len)) {
        return 1;
    }
    if (starts_with(p, len, "<!--", 4)) {
        return 2;
    }
    if (p[1] == '?') {
        return 3;
    }
    if (p[1] == '!' && len > 2 && is_ascii_letter(p[2])) {
        return 4;
    }
    if (starts_with(p, len, "<![CDATA[", 9)) {
        return 5;
    }
    if (html_start_6(p, len)) {
        return 6;
    }
    if (html_start_7(p, len)) {
        return 7;
    }
    return 0;
}

/*
 * End condition 1: the line contains "</" followed by a name of start
 * condition 1 and '>'. Each offset is checked with a bounded amount of
 * work, so the search is linear in len.
 */
static bool html_end_1(const unsigned char *p, size_t len)
{
    size_t i;

    for (i = 0; i + 2 < len; i++) {
        if (p[i] == '<' && p[i + 1] == '/') {
            size_t n = raw_text_name_length(p + i + 2, len - i - 2);
            if (n != 0 && i + 2 + n < len && p[i + 2 + n] == '>') {
                return true;
            }
        }
    }
    return false;
}

bool amark_scan_html_end(const unsigned char *p, size_t len, unsigned type)
{
    switch (type) {
    case 1:
        return html_end_1(p, len);
    case 2:
        return contains(p, len, "-->", 3);
    case 3:
        return contains(p, len, "?>", 2);
    case 4:
        return contains(p, len, ">", 1);
    case 5:
        return contains(p, len, "]]>", 3);
    default:
        return false;
    }
}

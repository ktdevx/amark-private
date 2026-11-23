/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of reading one link reference definition from the start of a
 * paragraph (src/refdef.h): definitions on one line, definitions that break
 * the line in the label, before the destination, before the title and inside
 * the title, the three title delimiters, <> destinations, empty titles,
 * missing titles, the two ways a title can fail to match (on the line of the
 * destination: no definition; on the next line: a definition without a
 * title), destinations with parentheses and escapes, inputs that are no
 * definition, the limit, and the ranges of definitions inside containers,
 * whose line records the tests write by hand.
 */

#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "refdef.h"
#include "test_alloc.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * creates is kept in g_doc, and teardown destroys it and checks that no
 * block is left.
 */
#define BUFFER_SIZE 4096

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static unsigned char g_buffer[BUFFER_SIZE];
static size_t g_length;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_length = 0;
}

NAOS_TEARDOWN(counting)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * Creates g_doc with parse data over the bytes of a string literal, the
 * range being the whole content. Every line record is (end of the line's
 * content, 0) until a test writes it.
 */
static void create_doc(const char *bytes, size_t length)
{
    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
    memcpy(g_buffer, bytes, length);
    g_length = length;
    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, g_buffer,
                                              g_length, 0, g_length, NULL,
                                              &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc), AMARK_ERROR_NONE);
}

#define CREATE(s) create_doc((s), sizeof(s) - 1)

/*
 * The offset of the nth (from 0) occurrence of the string needle in the
 * content; it must exist.
 */
static size_t find_nth(const char *needle, size_t nth)
{
    size_t length = strlen(needle);
    size_t i;

    for (i = 0; i + length <= g_length; i++) {
        if (memcmp(g_buffer + i, needle, length) == 0) {
            if (nth == 0) {
                return i;
            }
            nth--;
        }
    }
    NAOS_REQUIRE_TRUE(false);
    return 0;
}

static size_t find(const char *needle)
{
    return find_nth(needle, 0);
}

/*
 * Writes the record of each line from 2 to last (every line when last is 0)
 * so that its content starts at its first byte: the record of a paragraph
 * line with no container prefix.
 */
static void set_plain_records(size_t last)
{
    size_t line;

    for (line = 2; last == 0 || line <= last; line++) {
        amark_line_bounds_t bounds;

        if (!amark_line_bounds(g_doc, line, &bounds)) {
            NAOS_REQUIRE_UINT_EQ(last, 0);
            return;
        }
        amark_line_set(g_doc, line, bounds.start, 0);
    }
}

/* The expected result of a definition. */
typedef struct expected {
    size_t label_start;
    size_t label_end;
    size_t destination_start;
    size_t destination_end;
    bool has_title;
    size_t title_start;
    size_t title_end;
    size_t last_line;
    size_t end;
} expected_t;

/*
 * Reads a definition at start with limit, checks that it is one with the
 * expected ranges, and that reading it allocated nothing.
 */
static void check_definition(size_t start, size_t limit,
                             const expected_t *want)
{
    amark_refdef_t got;
    size_t calls = g_state.malloc_calls;

    memset(&got, 0, sizeof(got));
    NAOS_REQUIRE_TRUE(amark_refdef_parse(g_doc, start, limit, &got));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    NAOS_CHECK_UINT_EQ(got.label.start, want->label_start);
    NAOS_CHECK_UINT_EQ(got.label.end, want->label_end);
    NAOS_CHECK_UINT_EQ(got.destination.start, want->destination_start);
    NAOS_CHECK_UINT_EQ(got.destination.end, want->destination_end);
    NAOS_CHECK_INT_EQ(got.has_title, want->has_title);
    if (want->has_title) {
        NAOS_CHECK_UINT_EQ(got.title.start, want->title_start);
        NAOS_CHECK_UINT_EQ(got.title.end, want->title_end);
    }
    NAOS_CHECK_UINT_EQ(got.last_line, want->last_line);
    NAOS_CHECK_UINT_EQ(got.end, want->end);
}

/* Checks that start to limit is no definition and *out is left alone. */
static void check_no_definition(size_t start, size_t limit)
{
    amark_refdef_t got;
    amark_refdef_t before;

    memset(&got, 0xA5, sizeof(got));
    memcpy(&before, &got, sizeof(got));
    NAOS_CHECK_FALSE(amark_refdef_parse(g_doc, start, limit, &got));
    NAOS_CHECK_TRUE(memcmp(&got, &before, sizeof(got)) == 0);
}

/* Checks that the whole content is no definition. */
static void check_no_definition_in(const char *bytes, size_t length)
{
    create_doc(bytes, length);
    set_plain_records(0);
    check_no_definition(0, g_length);
}

#define NO_DEFINITION(s) check_no_definition_in((s), sizeof(s) - 1)

/* ---- one line ----------------------------------------------------------- */

NAOS_TEST(one_line_with_double_quoted_title)
{
    expected_t want = {1, 4, 7, 11, true, 13, 18, 1, 19};

    /*         0123456789012345678 */
    CREATE("[foo]: /url \"title\"");
    check_definition(0, g_length, &want);
}

NAOS_TEST(one_line_with_single_quoted_title)
{
    expected_t want = {1, 4, 7, 11, true, 13, 18, 1, 19};

    CREATE("[foo]: /url 'title'");
    check_definition(0, g_length, &want);
}

NAOS_TEST(one_line_with_parenthesized_title)
{
    expected_t want = {1, 4, 7, 11, true, 13, 18, 1, 19};

    CREATE("[foo]: /url (title)");
    check_definition(0, g_length, &want);
}

NAOS_TEST(one_line_without_title)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url");
    check_definition(0, g_length, &want);
}

NAOS_TEST(trailing_white_space_ends_at_the_content_end)
{
    expected_t want = {1, 4, 7, 11, true, 13, 14, 1, 17};

    /* The content of the line ends before the line ending, after the
       trailing white space. */
    CREATE("[foo]: /url 't' \t\nnext");
    check_definition(0, find("\n"), &want);
}

NAOS_TEST(no_white_space_after_the_colon)
{
    expected_t want = {1, 4, 6, 10, false, 0, 0, 1, 10};

    CREATE("[foo]:/url");
    check_definition(0, g_length, &want);
}

NAOS_TEST(angle_destination_is_the_inside)
{
    expected_t want = {1, 8, 12, 18, true, 21, 26, 1, 27};

    /*      0         1         2      */
    /*      012345678901234567890123456 */
    CREATE("[Foo bar]: <my url> 'title'");
    check_definition(0, g_length, &want);
}

NAOS_TEST(empty_angle_destination_is_an_empty_range)
{
    expected_t want = {1, 4, 8, 8, false, 0, 0, 1, 9};

    CREATE("[foo]: <>");
    check_definition(0, g_length, &want);
}

NAOS_TEST(empty_title_is_written_and_empty)
{
    expected_t want = {1, 4, 7, 11, true, 13, 13, 1, 14};

    CREATE("[foo]: /url \"\"");
    check_definition(0, g_length, &want);
}

NAOS_TEST(empty_parenthesized_title)
{
    expected_t want = {1, 4, 7, 11, true, 13, 13, 1, 14};

    CREATE("[foo]: /url ()");
    check_definition(0, g_length, &want);
}

NAOS_TEST(escapes_in_label_destination_and_title)
{
    expected_t want;

    CREATE("[foo]: /url\\bar\\*baz \"foo\\\"bar\\baz\"");
    want.label_start = 1;
    want.label_end = 4;
    want.destination_start = 7;
    want.destination_end = find(" \"");
    want.has_title = true;
    want.title_start = find(" \"") + 2;
    want.title_end = g_length - 1;
    want.last_line = 1;
    want.end = g_length;
    check_definition(0, g_length, &want);
}

NAOS_TEST(escaped_bracket_in_label_and_parentheses_in_destination)
{
    expected_t want;

    CREATE("[Foo*bar\\]]:my_(url) 'title (with parens)'");
    want.label_start = 1;
    want.label_end = find("]:");
    want.destination_start = find("my_");
    want.destination_end = find(" '");
    want.has_title = true;
    want.title_start = find(" '") + 2;
    want.title_end = g_length - 1;
    want.last_line = 1;
    want.end = g_length;
    check_definition(0, g_length, &want);
}

NAOS_TEST(nested_and_escaped_parentheses_in_destination)
{
    expected_t want = {1, 2, 5, 17, false, 0, 0, 1, 17};

    /*      012345678901234567 */
    CREATE("[a]: x(y(z))\\(q\\)");
    check_definition(0, g_length, &want);
}

NAOS_TEST(destination_may_contain_quotes)
{
    expected_t want = {1, 4, 7, 18, false, 0, 0, 1, 18};

    /* No white space before the quote: it belongs to the destination. */
    CREATE("[foo]: /url\"title\"");
    check_definition(0, g_length, &want);
}

NAOS_TEST(escaped_delimiters_inside_titles)
{
    expected_t want = {1, 2, 5, 6, true, 8, 14, 1, 15};

    /*      0123456789012345 */
    CREATE("[a]: u (x\\)\\(y)");
    check_definition(0, g_length, &want);
}

NAOS_TEST(start_is_not_the_first_byte)
{
    expected_t want = {4, 7, 10, 14, false, 0, 0, 1, 14};

    CREATE("   [foo]: /url");
    check_definition(3, g_length, &want);
}

/* ---- several lines ------------------------------------------------------ */

NAOS_TEST(line_break_inside_the_label)
{
    expected_t want;

    CREATE("[\nfoo\n]: /url\nbar");
    set_plain_records(4);
    want.label_start = 1;
    want.label_end = find("]");
    want.destination_start = find("/url");
    want.destination_end = find("/url") + 4;
    want.has_title = false;
    want.title_start = 0;
    want.title_end = 0;
    want.last_line = 3;
    want.end = find("\nbar");
    check_definition(0, g_length, &want);
}

NAOS_TEST(line_break_before_the_destination)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 2, 11};

    CREATE("[foo]:\n/url");
    set_plain_records(2);
    check_definition(0, g_length, &want);
}

NAOS_TEST(line_break_before_the_angle_destination_and_title)
{
    expected_t want;

    CREATE("[Foo bar]:\n<my url>\n'title'");
    set_plain_records(3);
    want.label_start = 1;
    want.label_end = 8;
    want.destination_start = find("my url");
    want.destination_end = find(">");
    want.has_title = true;
    want.title_start = find("title");
    want.title_end = g_length - 1;
    want.last_line = 3;
    want.end = g_length;
    check_definition(0, g_length, &want);
}

NAOS_TEST(line_break_before_the_title)
{
    expected_t want = {1, 4, 7, 11, true, 15, 20, 2, 21};

    /*      01234567890 1234567890 */
    CREATE("[foo]: /url\n  \"title\"  ");
    set_plain_records(2);
    want.end = g_length;
    check_definition(0, g_length, &want);
}

NAOS_TEST(indented_definition_with_white_space_on_every_line)
{
    expected_t want;

    CREATE("   [foo]: \n      /url  \n           'the title'  ");
    set_plain_records(3);
    want.label_start = 4;
    want.label_end = 7;
    want.destination_start = find("/url");
    want.destination_end = find("/url") + 4;
    want.has_title = true;
    want.title_start = find("the title");
    want.title_end = find("the title") + 9;
    want.last_line = 3;
    want.end = g_length;
    check_definition(3, g_length, &want);
}

NAOS_TEST(line_breaks_inside_the_title)
{
    expected_t want;

    CREATE("[foo]: /url '\ntitle\nline1\nline2\n'");
    set_plain_records(5);
    want.label_start = 1;
    want.label_end = 4;
    want.destination_start = 7;
    want.destination_end = 11;
    want.has_title = true;
    want.title_start = 13;
    want.title_end = g_length - 1;
    want.last_line = 5;
    want.end = g_length;
    check_definition(0, g_length, &want);
}

NAOS_TEST(crlf_and_cr_line_endings)
{
    expected_t want;

    CREATE("[foo]:\r\n/url\r'ti\r\ntle'\r\nrest");
    set_plain_records(5);
    want.label_start = 1;
    want.label_end = 4;
    want.destination_start = find("/url");
    want.destination_end = find("/url") + 4;
    want.has_title = true;
    want.title_start = find("'") + 1;
    want.title_end = find("le'") + 2;
    want.last_line = 4;
    want.end = find("le'") + 3;
    check_definition(0, g_length, &want);
}

NAOS_TEST(definition_ends_and_the_next_line_is_left)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url\nbar");
    set_plain_records(2);
    check_definition(0, g_length, &want);
}

NAOS_TEST(definition_followed_by_another_definition)
{
    expected_t want = {1, 4, 7, 15, true, 17, 20, 1, 21};

    CREATE("[foo]: /foo-url \"foo\"\n[bar]: /bar-url\n  \"bar\"");
    set_plain_records(3);
    check_definition(0, g_length, &want);
    want.label_start = find("bar]");
    want.label_end = find("bar]") + 3;
    want.destination_start = find("/bar-url");
    want.destination_end = find("/bar-url") + 8;
    want.title_start = find("bar\"");
    want.title_end = find("bar\"") + 3;
    want.last_line = 3;
    want.end = g_length;
    check_definition(find("[bar]"), g_length, &want);
}

/* ---- titles that do not match ------------------------------------------- */

NAOS_TEST(text_after_title_on_the_destination_line_is_no_definition)
{
    CREATE("[foo]: /url \"title\" ok");
    check_no_definition(0, g_length);
}

NAOS_TEST(text_after_title_on_the_next_line_leaves_a_definition)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url\n\"title\" ok");
    set_plain_records(2);
    check_definition(0, g_length, &want);
}

NAOS_TEST(unclosed_title_on_the_destination_line_is_no_definition)
{
    CREATE("[foo]: /url 'title\nwith no end");
    set_plain_records(2);
    check_no_definition(0, g_length);
}

NAOS_TEST(unclosed_title_on_the_next_line_leaves_a_definition)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url\n'title\nwith no end");
    set_plain_records(3);
    check_definition(0, g_length, &want);
}

NAOS_TEST(title_cannot_contain_a_blank_line)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url\n'title\n  \nwith blank line'");
    set_plain_records(4);
    check_definition(0, g_length, &want);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    CREATE("[foo]: /url 'title\n  \nwith blank line'");
    set_plain_records(3);
    check_no_definition(0, g_length);
}

NAOS_TEST(parenthesized_title_cannot_contain_an_open_parenthesis)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url\n(a(b)");
    set_plain_records(2);
    check_definition(0, g_length, &want);
}

NAOS_TEST(next_line_that_is_no_title_leaves_a_definition)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url\nbar \"baz\"");
    set_plain_records(2);
    check_definition(0, g_length, &want);
}

NAOS_TEST(title_needs_white_space_before_it)
{
    CREATE("[foo]: <bar>(baz)");
    check_no_definition(0, g_length);
}

/* ---- no definition ------------------------------------------------------ */

NAOS_TEST(missing_destination)
{
    NO_DEFINITION("[foo]:");
}

NAOS_TEST(missing_destination_on_the_next_line)
{
    CREATE("[foo]:\n   ");
    set_plain_records(2);
    check_no_definition(0, g_length);
}

NAOS_TEST(two_line_endings_before_the_destination)
{
    CREATE("[foo]:\n\n/url");
    set_plain_records(3);
    check_no_definition(0, g_length);
}

NAOS_TEST(two_line_endings_before_the_title)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url\n\n'title'");
    set_plain_records(3);
    check_definition(0, g_length, &want);
}

NAOS_TEST(no_colon_right_after_the_label)
{
    NO_DEFINITION("[foo] : /url");
}

NAOS_TEST(no_label)
{
    NO_DEFINITION("[]: /url");
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NO_DEFINITION("[foo: /url");
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NO_DEFINITION("[ \t]: /url");
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NO_DEFINITION("foo]: /url");
}

NAOS_TEST(angle_destination_with_line_ending_or_angle)
{
    CREATE("[foo]: <a\nb>");
    set_plain_records(2);
    check_no_definition(0, g_length);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NO_DEFINITION("[foo]: <a<b>");
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NO_DEFINITION("[foo]: <ab");
}

NAOS_TEST(angle_destination_with_escaped_angles)
{
    expected_t want = {1, 4, 8, 14, false, 0, 0, 1, 15};

    /*      012345678901234 */
    CREATE("[foo]: <a\\<b\\>>");
    check_definition(0, g_length, &want);
}

NAOS_TEST(text_after_angle_destination)
{
    NO_DEFINITION("[foo]: <a>b");
}

NAOS_TEST(unbalanced_parentheses_in_destination)
{
    NO_DEFINITION("[foo]: /u(rl");
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NO_DEFINITION("[foo]: /u)rl");
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NO_DEFINITION("[foo]: )");
}

NAOS_TEST(control_character_in_destination)
{
    NO_DEFINITION("[foo]: /u\x01rl");
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NO_DEFINITION("[foo]: /u\x7Frl");
}

NAOS_TEST(start_is_not_a_bracket)
{
    CREATE(" [foo]: /url");
    check_no_definition(0, g_length);
}

/* ---- the limit ---------------------------------------------------------- */

NAOS_TEST(nothing_is_read_at_or_past_the_limit)
{
    expected_t want = {1, 4, 7, 11, false, 0, 0, 1, 11};

    CREATE("[foo]: /url\n\"title\"");
    set_plain_records(2);
    check_definition(0, find("\n"), &want);
    /* The closing quote is past the limit. */
    check_no_definition(0, 0);
    check_definition(0, g_length - 1, &want);
}

NAOS_TEST(label_ending_past_the_limit)
{
    CREATE("[foo]: /url");
    check_no_definition(0, 4);
    check_no_definition(0, 5);
}

/* ---- containers --------------------------------------------------------- */

NAOS_TEST(definition_inside_a_block_quote)
{
    expected_t want;

    /* The records start the content of lines 2 to 4 after "> ". */
    CREATE("> [fo\n> o]:\n> /url\n> 'ti\n> tle'\n> rest");
    amark_line_set(g_doc, 2, find_nth("> ", 1) + 2, 0);
    amark_line_set(g_doc, 3, find_nth("> ", 2) + 2, 0);
    amark_line_set(g_doc, 4, find_nth("> ", 3) + 2, 0);
    amark_line_set(g_doc, 5, find_nth("> ", 4) + 2, 0);
    amark_line_set(g_doc, 6, find_nth("> ", 5) + 2, 0);
    want.label_start = 3;
    want.label_end = find("]");
    want.destination_start = find("/url");
    want.destination_end = find("/url") + 4;
    want.has_title = true;
    want.title_start = find("'") + 1;
    want.title_end = find("tle'") + 3;
    want.last_line = 5;
    want.end = find("tle'") + 4;
    check_definition(2, g_length, &want);
}

NAOS_TEST(prefix_bytes_are_not_read_as_content)
{
    expected_t want = {3, 6, 9, 13, false, 0, 0, 1, 13};

    /* With the record of line 2 as created (the end of its content), the
       whole line is skipped and the definition ends on line 1; with the
       record after "> ", "'x'" is the title. */
    CREATE("> [foo]: /url\n> 'x'");
    check_definition(2, g_length, &want);
    amark_line_set(g_doc, 2, find("'x'"), 0);
    want.has_title = true;
    want.title_start = find("'x'") + 1;
    want.title_end = find("'x'") + 2;
    want.last_line = 2;
    want.end = g_length;
    check_definition(2, g_length, &want);
}

NAOS_TEST(prefix_inside_a_list_item_with_a_tab_record)
{
    expected_t want;

    /* The record of line 2 starts at the tab with pad 2. */
    CREATE("- [foo]:\n \t/url");
    amark_line_set(g_doc, 2, find("\t"), 2);
    want.label_start = 3;
    want.label_end = 6;
    want.destination_start = find("/url");
    want.destination_end = g_length;
    want.has_title = false;
    want.title_start = 0;
    want.title_end = 0;
    want.last_line = 2;
    want.end = g_length;
    check_definition(2, g_length, &want);
}

NAOS_TEST(long_input_allocates_nothing)
{
    size_t calls;
    amark_refdef_t got;

    CREATE("[foo]: /url\n'a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq'");
    set_plain_records(18);
    calls = g_state.malloc_calls;
    NAOS_CHECK_TRUE(amark_refdef_parse(g_doc, 0, g_length, &got));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    NAOS_CHECK_TRUE(got.has_title);
    NAOS_CHECK_UINT_EQ(got.last_line, 18);
}

NAOS_SUITE(refdef_one_line,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(one_line_with_double_quoted_title),
    NAOS_ADD_TEST(one_line_with_single_quoted_title),
    NAOS_ADD_TEST(one_line_with_parenthesized_title),
    NAOS_ADD_TEST(one_line_without_title),
    NAOS_ADD_TEST(trailing_white_space_ends_at_the_content_end),
    NAOS_ADD_TEST(no_white_space_after_the_colon),
    NAOS_ADD_TEST(angle_destination_is_the_inside),
    NAOS_ADD_TEST(empty_angle_destination_is_an_empty_range),
    NAOS_ADD_TEST(empty_title_is_written_and_empty),
    NAOS_ADD_TEST(empty_parenthesized_title),
    NAOS_ADD_TEST(escapes_in_label_destination_and_title),
    NAOS_ADD_TEST(escaped_bracket_in_label_and_parentheses_in_destination),
    NAOS_ADD_TEST(nested_and_escaped_parentheses_in_destination),
    NAOS_ADD_TEST(destination_may_contain_quotes),
    NAOS_ADD_TEST(escaped_delimiters_inside_titles),
    NAOS_ADD_TEST(start_is_not_the_first_byte)
)

NAOS_SUITE(refdef_lines,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(line_break_inside_the_label),
    NAOS_ADD_TEST(line_break_before_the_destination),
    NAOS_ADD_TEST(line_break_before_the_angle_destination_and_title),
    NAOS_ADD_TEST(line_break_before_the_title),
    NAOS_ADD_TEST(indented_definition_with_white_space_on_every_line),
    NAOS_ADD_TEST(line_breaks_inside_the_title),
    NAOS_ADD_TEST(crlf_and_cr_line_endings),
    NAOS_ADD_TEST(definition_ends_and_the_next_line_is_left),
    NAOS_ADD_TEST(definition_followed_by_another_definition)
)

NAOS_SUITE(refdef_titles,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(text_after_title_on_the_destination_line_is_no_definition),
    NAOS_ADD_TEST(text_after_title_on_the_next_line_leaves_a_definition),
    NAOS_ADD_TEST(unclosed_title_on_the_destination_line_is_no_definition),
    NAOS_ADD_TEST(unclosed_title_on_the_next_line_leaves_a_definition),
    NAOS_ADD_TEST(title_cannot_contain_a_blank_line),
    NAOS_ADD_TEST(parenthesized_title_cannot_contain_an_open_parenthesis),
    NAOS_ADD_TEST(next_line_that_is_no_title_leaves_a_definition),
    NAOS_ADD_TEST(title_needs_white_space_before_it)
)

NAOS_SUITE(refdef_rejected,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(missing_destination),
    NAOS_ADD_TEST(missing_destination_on_the_next_line),
    NAOS_ADD_TEST(two_line_endings_before_the_destination),
    NAOS_ADD_TEST(two_line_endings_before_the_title),
    NAOS_ADD_TEST(no_colon_right_after_the_label),
    NAOS_ADD_TEST(no_label),
    NAOS_ADD_TEST(angle_destination_with_line_ending_or_angle),
    NAOS_ADD_TEST(angle_destination_with_escaped_angles),
    NAOS_ADD_TEST(text_after_angle_destination),
    NAOS_ADD_TEST(unbalanced_parentheses_in_destination),
    NAOS_ADD_TEST(control_character_in_destination),
    NAOS_ADD_TEST(start_is_not_a_bracket),
    NAOS_ADD_TEST(nothing_is_read_at_or_past_the_limit),
    NAOS_ADD_TEST(label_ending_past_the_limit)
)

NAOS_SUITE(refdef_containers,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(definition_inside_a_block_quote),
    NAOS_ADD_TEST(prefix_bytes_are_not_read_as_content),
    NAOS_ADD_TEST(prefix_inside_a_list_item_with_a_tab_record),
    NAOS_ADD_TEST(long_input_allocates_nothing)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(refdef_one_line),
    NAOS_ADD_SUITE(refdef_lines),
    NAOS_ADD_SUITE(refdef_titles),
    NAOS_ADD_SUITE(refdef_rejected),
    NAOS_ADD_SUITE(refdef_containers)
)

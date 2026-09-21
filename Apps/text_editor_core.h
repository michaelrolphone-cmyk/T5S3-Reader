#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TE_CAPACITY 16384u
#define TE_CLIPBOARD 2048u

typedef struct {
    char text[TE_CAPACITY + 1];
    char clipboard[TE_CLIPBOARD + 1];
    size_t length, cursor, anchor, clip_length;
    bool selected, dirty;
} te_document;

static inline size_t te_first(const te_document *d) {
    return d->selected && d->anchor < d->cursor ? d->anchor : d->cursor;
}
static inline size_t te_last(const te_document *d) {
    return d->selected && d->anchor > d->cursor ? d->anchor : d->cursor;
}
static inline bool te_has_selection(const te_document *d) {
    return d->selected && d->anchor != d->cursor;
}
static inline void te_move(te_document *d, size_t target, bool extend) {
    if (target > d->length) target = d->length;
    if (extend && !d->selected) d->anchor = d->cursor;
    d->selected = extend;
    d->cursor = target;
}
static inline bool te_replace(te_document *d, const char *bytes, size_t count) {
    const size_t first = te_first(d), last = te_last(d);
    const size_t removed = last - first;
    if (count > TE_CAPACITY - (d->length - removed) || (!bytes && count)) return false;
    const size_t tail = d->length - last + 1;
    // App loader does not export memmove: overlap-safe bounded local copy.
    if (first + count > last) {
        for (size_t i = tail; i; --i)
            d->text[first + count + i - 1] = d->text[last + i - 1];
    } else {
        for (size_t i = 0; i < tail; ++i)
            d->text[first + count + i] = d->text[last + i];
    }
    if (count) memcpy(d->text + first, bytes, count);
    d->length = d->length - removed + count;
    d->cursor = first + count;
    d->anchor = d->cursor;
    d->selected = false;
    if (count || removed) d->dirty = true;
    return true;
}
static inline bool te_character(te_document *d, char ch) {
    if (ch != '\n' && ch != '\t' && (ch < 32 || ch > 126)) return false;
    return te_replace(d, &ch, 1);
}
static inline void te_backspace(te_document *d) {
    if (te_has_selection(d)) { (void)te_replace(d, 0, 0); return; }
    if (d->cursor) {
        d->selected = true; d->anchor = d->cursor - 1;
        (void)te_replace(d, 0, 0);
    }
}
static inline void te_delete(te_document *d) {
    if (te_has_selection(d)) { (void)te_replace(d, 0, 0); return; }
    if (d->cursor < d->length) {
        d->selected = true; d->anchor = d->cursor + 1;
        (void)te_replace(d, 0, 0);
    }
}
static inline size_t te_line_start(const te_document *d, size_t at) {
    if (at > d->length) at = d->length;
    while (at && d->text[at - 1] != '\n') --at;
    return at;
}
static inline size_t te_line_end(const te_document *d, size_t at) {
    while (at < d->length && d->text[at] != '\n') ++at;
    return at;
}
static inline void te_vertical(te_document *d, bool down, bool extend) {
    const size_t begin = te_line_start(d, d->cursor);
    const size_t column = d->cursor - begin;
    const size_t end = te_line_end(d, d->cursor);
    size_t target;
    if (down) {
        if (end == d->length) return;
        target = end + 1;
    } else {
        if (!begin) return;
        target = te_line_start(d, begin - 1);
    }
    const size_t limit = te_line_end(d, target);
    te_move(d, column > limit - target ? limit : target + column, extend);
}
static inline void te_copy(te_document *d) {
    if (!te_has_selection(d)) return;
    const size_t count = te_last(d) - te_first(d);
    if (count > TE_CLIPBOARD) return;
    memcpy(d->clipboard, d->text + te_first(d), count);
    d->clipboard[count] = 0;
    d->clip_length = count;
}
static inline void te_cut(te_document *d) {
    if (!te_has_selection(d) || te_last(d) - te_first(d) > TE_CLIPBOARD) return;
    te_copy(d);
    (void)te_replace(d, 0, 0);
}
static inline bool te_paste(te_document *d) {
    return d->clip_length && te_replace(d, d->clipboard, d->clip_length);
}
static inline void te_reset(te_document *d) {
    d->length = d->cursor = d->anchor = 0;
    d->text[0] = 0;
    d->selected = d->dirty = false;
}
// Validate before mutation: rejected files must preserve the current buffer.
// Reject binary and non-ASCII files; normalize CRLF line endings to LF.
static inline bool te_import(te_document *d, const char *source, size_t length) {
    if ((!source && length) || length > TE_CAPACITY) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char ch = (unsigned char)source[i];
        if (ch == '\r' && i + 1 < length && source[i + 1] == '\n') continue;
        if (ch != '\n' && ch != '\t' && (ch < 32 || ch > 126)) return false;
    }
    size_t output = 0;
    for (size_t i = 0; i < length; ++i) {
        if (source[i] == '\r' && i + 1 < length && source[i + 1] == '\n') continue;
        d->text[output++] = source[i];
    }
    d->text[output] = 0;
    d->length = output;
    d->cursor = d->anchor = 0;
    d->selected = d->dirty = false;
    return true;
}

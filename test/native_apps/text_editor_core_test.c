#include "../../Apps/text_editor_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static te_document doc;
int main(void) {
    te_reset(&doc);
    assert(te_import(&doc, "one\r\ntwo\r\n", 10));
    assert(strcmp(doc.text, "one\ntwo\n") == 0 && doc.length == 8);
    assert(!doc.dirty);
    te_move(&doc, 3, false);
    assert(te_character(&doc, '!'));
    assert(strcmp(doc.text, "one!\ntwo\n") == 0 && doc.dirty);
    te_backspace(&doc);
    assert(strcmp(doc.text, "one\ntwo\n") == 0);
    te_move(&doc, 0, false);
    te_vertical(&doc, true, false);
    assert(doc.cursor == 4);
    te_move(&doc, 0, false);
    te_move(&doc, 3, true);
    te_copy(&doc);
    assert(doc.clip_length == 3 && strcmp(doc.clipboard, "one") == 0);
    te_cut(&doc);
    assert(strcmp(doc.text, "\ntwo\n") == 0);
    assert(te_paste(&doc));
    assert(strcmp(doc.text, "one\ntwo\n") == 0);
    te_move(&doc, doc.length, false);
    te_move(&doc, 0, true);
    te_delete(&doc);
    assert(doc.length == 0 && doc.text[0] == 0);
    assert(te_import(&doc, "Keep me", 7));
    assert(!te_import(&doc, "bad\0data", 8));
    assert(strcmp(doc.text, "Keep me") == 0 && doc.length == 7);
    assert(!te_import(&doc, "\xff", 1));
    assert(strcmp(doc.text, "Keep me") == 0);
    te_reset(&doc);
    for (size_t i = 0; i < TE_CAPACITY; ++i) assert(te_character(&doc, 'x'));
    assert(!te_character(&doc, 'y'));
    assert(doc.length == TE_CAPACITY && doc.text[TE_CAPACITY] == 0);
    te_move(&doc, 0, false);
    te_delete(&doc);
    assert(doc.length == TE_CAPACITY - 1);
    puts("text_editor_core_test: PASS");
    return 0;
}

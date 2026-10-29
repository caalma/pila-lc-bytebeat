// -------------------------------------------------------------------------
// strtab.c
// -------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "strtab.h"
#include "diag.h"

static char strings[MAX_STRINGS][MAX_STRING_LEN];
static int  n_strings; // persiste entre cargas — nunca se resetea

int strtab_count(void) { return n_strings; }

int strtab_intern(const char *s) {
    for (int i = 0; i < n_strings; i++)
        if (strcmp(strings[i], s) == 0) return i;
    if (n_strings >= MAX_STRINGS) {
        fprintf(stderr, "Limite de %d strings alcanzado, \"%s\" se ignora\n",
                MAX_STRINGS, s);
        diag_add("Limite de %d strings alcanzado, se ignora un literal", MAX_STRINGS);
        return -1;
    }
    strncpy(strings[n_strings], s, MAX_STRING_LEN - 1);
    strings[n_strings][MAX_STRING_LEN - 1] = '\0';
    return n_strings++;
}

const char *strtab_get(int index) {
    if (index < 0 || index >= n_strings) return "";
    return strings[index];
}

int strtab_is_valid(int index) {
    return index >= 0 && index < n_strings;
}

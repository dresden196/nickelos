// Prints what libhangul composes from each line of keys (keyboard id argv[1]):
// commit strings and the final preedit, as UTF-8, '|' between syllables
// committed separately; unprocessed keys go through as themselves.
#include <hangul.h>
#include <stdio.h>
#include <string.h>
static void put(const ucschar* s) {
  for (; *s; s++) {
    unsigned c = *s;
    if (c < 0x80) putchar(c);
    else if (c < 0x800) { putchar(0xC0 | c >> 6); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | c >> 12); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
  }
}
int main(int argc, char** argv) {
  HangulInputContext* hic = hangul_ic_new(argv[1]);
  char line[256];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    hangul_ic_reset(hic);
    for (char* p = line; *p; p++) {
      bool ok = hangul_ic_process(hic, *p);
      const ucschar* commit = hangul_ic_get_commit_string(hic);
      if (*commit) { put(commit); }
      if (!ok) { putchar(*p); }
    }
    put(hangul_ic_flush(hic));
    putchar('\n');
  }
  return 0;
}

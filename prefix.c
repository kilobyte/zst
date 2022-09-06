#include <strings.h>
#include <string.h>
#include "prefix.h"

int match_suffix(const char *txt, const char *ext)
{
    int tl,el;

    tl=strlen(txt);
    el=strlen(ext);
    if (tl<=el)
        return 0;
    txt+=tl-el;
    return !strncmp(txt, ext, el);
}

/* Host shim of source/utils/logger.h (Alice Vita loader tests). */
#include <stdio.h>
#define l_fatal(...)  (fprintf(stderr, "[fatal] " __VA_ARGS__), fputc('\n', stderr))
#define l_error(...)  (fprintf(stderr, "[error] " __VA_ARGS__), fputc('\n', stderr))
#define l_notice(...) (fprintf(stderr, "[notice] " __VA_ARGS__), fputc('\n', stderr))
#define l_stub(...)   (fprintf(stderr, "[stub] " __VA_ARGS__), fputc('\n', stderr))

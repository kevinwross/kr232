/* Guest standard I/O on the display. printf writes through the C runtime's SYS write
   to the terminal page and getchar polls SYS read for host display input. The run
   queues "Z" as a scripted key and then closes the source, so the first read must be
   that byte and the second must be end of input. */
#include <stdio.h>

int main(void) {
    const int first = getchar();
    const int second = getchar();
    printf("first=%c second=%s\n", first < 0 ? '?' : (char)first,
           second < 0 ? "eof" : "byte");
    return 0;
}

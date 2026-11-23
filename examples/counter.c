/* counts from 1 to five and prints them: a small program to load in the debugger */
#include <kr232/cons.h>

int total(int a, int b) {
    return a + b;
}

int main(void) {
    int sum = 0;
    int i;
    for (i = 1; i <= 5; ++i) {
        sum = total(sum, i);
        cons_puts("count\n");
    }
    return sum;
}

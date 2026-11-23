/* sums the first ten squares and returns the total: pure computation, so it needs
   nothing but the runtime start-up and loads straight from source. */
int square(int n) {
    return n * n;
}

int main(void) {
    int total = 0;
    int i;
    for (i = 1; i <= 10; ++i) {
        total = total + square(i);
    }
    return total;
}

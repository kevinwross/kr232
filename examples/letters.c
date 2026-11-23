/* copies a string into a buffer and counts its characters. */
int length(const char* text) {
    int n = 0;
    while (text[n] != 0) {
        n = n + 1;
    }
    return n;
}

int main(void) {
    char buffer[32];
    int i;
    for (i = 0; i < 26; ++i) {
        buffer[i] = 'a' + i;
    }
    buffer[26] = 0;
    return length(buffer);
}

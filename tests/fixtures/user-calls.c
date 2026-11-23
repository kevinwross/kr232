/* Calls every entry the kernel serves and checks the STATUS each one returns.
 *
 * The device answers bit 0 of STATUS with ERROR. A drawing call that fails must
 * not leave the program looking successful, which is how two frame goldens were
 * recorded against a device that drew nothing, so each call below states the
 * answer it must give: success for the calls that draw, and the documented
 * refusal for DRAW_TEXT, whose atlas belongs to the terminal page.
 *
 * The exit status names the first failing expectation by its bit, so a failure
 * says which call lied.
 */
extern int __kr232_syscall6(int number, ...);

#define KR232_CALL_READ 2
#define KR232_CALL_FILL 16
#define KR232_CALL_FLIP 17
#define KR232_CALL_COPY 18
#define KR232_CALL_LINE 19
#define KR232_CALL_TEXT 20
#define KR232_CALL_UPLOAD 21

static unsigned block[6];
static unsigned source[4];
static unsigned buffer[2];
static int failures;

static int status_of(int number) {
    return __kr232_syscall6(number, (int)block, 0, 0, 0, 0);
}

static void expect(int number, int failure_bit, int wanted_error) {
    const int status = status_of(number) & 1;
    if (status != wanted_error) {
        failures = failures | failure_bit;
    }
}

int main(void) {
    (void)__kr232_syscall6(KR232_CALL_FILL, 0, 0, 16, 16, 255);

    block[0] = 0;
    block[1] = 0;
    block[2] = 15;
    block[3] = 15;
    block[4] = 0x0000FF00;
    expect(KR232_CALL_LINE, 1, 0);

    block[0] = 0;
    block[1] = 0;
    block[2] = 0;
    block[3] = 8;
    block[4] = 8;
    expect(KR232_CALL_COPY, 2, 0);

    source[0] = 0x000000FF;
    source[1] = 0x000000FF;
    source[2] = 0x000000FF;
    source[3] = 0x000000FF;
    block[0] = (unsigned)source;
    block[1] = 0;
    block[2] = 4;
    block[3] = 4;
    block[4] = 8;
    expect(KR232_CALL_UPLOAD, 4, 0);

    block[0] = 0;
    block[1] = 0;
    block[2] = (unsigned)buffer;
    block[3] = 0;
    block[4] = 0x00FFFFFF;
    block[5] = 0;
    expect(KR232_CALL_TEXT, 8, 1);   /* documented: no atlas, so it refuses */

    (void)__kr232_syscall6(KR232_CALL_READ, (int)buffer, 4, 0, 0, 0);
    (void)__kr232_syscall6(KR232_CALL_FLIP, 0, 0, 0, 0, 0);
    return failures;
}

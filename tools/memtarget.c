// memtarget - a controlled target for verifying the memory scanner.
//
// Holds a single 4-byte value at a fixed, printed address and lets you change
// it interactively. That makes it possible to check the scanner against
// ground truth: the address it finds must match the address printed here, and
// the increased/decreased/unchanged filters must behave as the value moves.
//
// Build (Windows):  cl /O2 memtarget.c
// Build (Linux):    cc -O2 -o memtarget memtarget.c
//
// It only ever touches its own memory. Run it as a normal user.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Not static/const, and touched at runtime, so the optimiser cannot fold it
// away or move it into read-only memory.
volatile unsigned int g_value = 1337;
volatile double       g_double = 3.5;
char                  g_text[64] = "SCANME-DMA-EDITOR";

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("memtarget - scanner test target\n");
    printf("===============================\n");
    printf("  uint32 at %p  = %u\n",   (void*)&g_value,  g_value);
    printf("  double at %p  = %.3f\n", (void*)&g_double, g_double);
    printf("  string at %p  = \"%s\"\n", (void*)g_text,  g_text);
    printf("\nThe scanner should find exactly these addresses.\n\n");
    printf("commands:  + increase   - decrease   = leave unchanged\n");
    printf("           <number> set uint32   d <number> set double\n");
    printf("           s <text>  set string  p print   q quit\n\n");

    char line[128];
    while (1) {
        printf("[value=%u double=%.3f] > ", g_value, g_double);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == 'q') {
            break;
        } else if (line[0] == '+') {
            g_value += (line[1] ? (unsigned)atoi(line + 1) : 1);
        } else if (line[0] == '-') {
            g_value -= (line[1] ? (unsigned)atoi(line + 1) : 1);
        } else if (line[0] == '=') {
            /* deliberately unchanged */
        } else if (line[0] == 'd') {
            g_double = atof(line + 1);
        } else if (line[0] == 's') {
            const char* p = line + 1;
            while (*p == ' ') p++;
            snprintf(g_text, sizeof(g_text), "%s", p);
        } else if (line[0] == 'p') {
            printf("  uint32 at %p = %u\n",   (void*)&g_value,  g_value);
            printf("  double at %p = %.3f\n", (void*)&g_double, g_double);
            printf("  string at %p = \"%s\"\n", (void*)g_text,  g_text);
        } else if (line[0] >= '0' && line[0] <= '9') {
            g_value = (unsigned)strtoul(line, NULL, 0);
        }
    }
    return 0;
}

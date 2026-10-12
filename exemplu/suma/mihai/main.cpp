#include <cstdio>
/* scrie cu CRLF și spații la final de linie -> tot corect */
int main()
{
    FILE *f = fopen("suma.in", "r"), *g = fopen("suma.out", "wb");
    int n;
    long long s = 0, x;
    if (fscanf(f, "%d", &n) != 1) return 1;
    for (int i = 0; i < n; i++) { if (fscanf(f, "%lld", &x) != 1) break; s += x; }
    fprintf(g, "%lld   \r\n\r\n", s);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>

typedef unsigned long long ull_t;

static char path[] = "/dev/zero";

ull_t eat_all_malloc() {
    size_t size = UINT32_MAX;
    ull_t result = 0;
    int fd = open(path, O_RDONLY);
    while (size > 0) {
        while (1) {
            void* f = mmap(NULL, size, PROT_READ, MAP_PRIVATE | MAP_FILE, fd, 0);
            if (f == MAP_FAILED)
                break;

            result += size;
        }
        size >>= 1;
    }

    return result;
}

int main() {
    ull_t result = eat_all_malloc();

    printf("%llu\n", result);
    
    return 0;
}

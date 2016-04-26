#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "../vsd_driver/vsd_ioctl.h"
#include "vsd_device.h"

#define CHECKED_OPEN(DESCRIPTOR_NAME, FLAGS, FAILURE_RETURN) \
    int DESCRIPTOR_NAME = open(DEV_NAME, FLAGS); \
    if (DESCRIPTOR_NAME < 0) \
        return FAILURE_RETURN;

const char* DEV_NAME = "/dev/vsd";

static int fd = -1;

int vsd_init() {
    //fd = open(DEV_NAME, O_RDWR);
    //return fd > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    return 0;
}

int vsd_deinit() {
    //return close(fd);
    return 0;
}

int vsd_get_size(size_t *out_size) {
    CHECKED_OPEN(fd, 0, -1);
    vsd_ioctl_get_size_arg_t sz;
    int error = ioctl(fd, VSD_IOCTL_GET_SIZE, &sz);
    close(fd);
    if (error) {
        return error;
    }
    *out_size = sz.size;
    return EXIT_SUCCESS;
}

int vsd_set_size(size_t size) {
    CHECKED_OPEN(fd, 0, -1);
    vsd_ioctl_set_size_arg_t sz;
    sz.size = size;
    int error = ioctl(fd, VSD_IOCTL_SET_SIZE, &sz);

    close(fd);

    return error;
}

ssize_t vsd_read(char* dst, off_t offset, size_t size) {
     CHECKED_OPEN(fd, O_RDONLY, -1);
    int error = lseek(fd, offset, SEEK_SET);
    close(fd);
    if (error) {
        return error;
    }
    return read(fd, dst, size);
}

ssize_t vsd_write(const char* src, off_t offset, size_t size) {
    CHECKED_OPEN(fd, O_WRONLY, -1);
    int error = lseek(fd, offset, SEEK_SET);
    if (error) {
        return error;
    }
    ssize_t res= write(fd, src, size);
    close(fd);
    return res;
}

void* vsd_mmap(size_t offset) {
    CHECKED_OPEN(fd, O_RDWR, MAP_FAILED);
    size_t sz = -1;
    size_t page_sz = (size_t) getpagesize();
    if (offset % page_sz != 0) {
        return MAP_FAILED;
    }

    vsd_get_size(&sz);
    void* res= mmap(NULL, sz - offset, PROT_WRITE | PROT_READ, MAP_SHARED, fd, offset);

    close(fd);

    return res;
}

int vsd_munmap(void* addr, size_t offset) {
    
    size_t sz = -1;
    size_t page_sz = (size_t) getpagesize();
    if (offset % page_sz != 0) {
        return EXIT_FAILURE;
    }

    vsd_get_size(&sz);
    return munmap(addr, sz - offset);
}

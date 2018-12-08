#include "VerilogParser.hpp"

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "Usage:\n\t%s file\n", argv[0]);
        exit(-3);
    }

    int fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        fprintf(stderr, "error, could not open %s\n", argv[1]);
        exit(-3);
    }

    struct stat sb;
    fstat(fd, &sb);

    char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
    if(memblock == MAP_FAILED) {
        fprintf(stderr, "error, mmap failed\n");
        exit(-3);
    }

    VerilogParser vp;

    vp.parse(argv[1], memblock, sb.st_size);
    vp.printModules();
}


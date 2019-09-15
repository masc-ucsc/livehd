#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>

#include "prp.hpp"

int main(int argc, char **argv){
	if (argc != 2){
		printf("Usage: %s file\n", argv[0]);
		exit(1);
	}

	int fd = open(argv[1], O_RDONLY);
	if(fd < 0){
		printf("Error: couldn't open %s\n", argv[1]);
		exit(1);
	}

	struct stat sb;
	fstat(fd, &sb);

	char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
	if(memblock == MAP_FAILED){
		printf("Error: mmap failed.\n");
		exit(1);
	}

	Prp scanner;
	scanner.parse(argv[1], memblock);
}


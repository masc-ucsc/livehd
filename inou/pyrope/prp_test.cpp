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

	Prp scanner;

	scanner.parse_file(argv[1]);

}

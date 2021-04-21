#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include "../tfs.h"
#include "../block.h"

#define TESTDIR "/tmp/rmcf/mountdir"

int main(int argc, char **argv) {
	char* buff = malloc(BLOCK_SIZE * 4);
	buff = "hello\n";
	//memset(buff, 0, BLOCK_SIZE * 4);
	printf("buffer is %s\n", buff);
	
}

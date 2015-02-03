#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <errno.h>
#include "heap_alloc.h"
using namespace shark;

int main(int argc, char* argv[])
{
	///*
	char* mem = (char*)heap_alloc(4);
	snprintf(mem, 4, "%s", "hello");
	mem = (char*)heap_realloc(mem, 8);
	mem = (char*)heap_realloc(mem, 32);
	mem = (char*)heap_realloc(mem, 80);
	printf("mem realloc info : %s\n", mem);
	heap_free(mem);
	heap_report();
	//*/
	
	return 0;
}

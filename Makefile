# Compiler, change if needed.
CC=clang

# System specicific CFLAGS.
CFLAGS_OSX=-std=c99 -framework OpenCL
CFLAGS_LINUX=-std=c99 -lopencl

# Set effective CFLAGS to system CFLAGS, change if needed.
CFLAGS=${CFLAGS_OSX}

clc : clc.c
	${CC} ${CFLAGS} clc.c -o clc

clean :
	rm -f clc

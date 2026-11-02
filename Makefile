CC = gcc
CFLAGS = -Wall -Wextra 
VENDOR_CFLAGS = ""
VENDOR_DIR = "./vendor"

tomlc:
	${CC} ${VENDOR_CFLAGS} -isystem ${VENDOR_DIR}/tomlc17-R261003/

all:
	${CC} ${CFLAGS} -o main ./src/main.c

clean:
	rm main

.PHONY:
	all clean

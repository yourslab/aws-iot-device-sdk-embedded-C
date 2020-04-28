CC = gcc

INCLUDE_DIRS = -I . \
               -I ../../../libraries/standard/http/include

SRC_FILES = http_demo_plaintext.c \
            $(shell find ../../../libraries/standard/http/src -name '*.c')

FLAGS = -g -O0 -Wall -Wextra -Wpedantic

all:
	$(CC) $(FLAGS) $(INCLUDE_DIRS) $(SRC_FILES) -o http_demo_plaintext

clean:
	rm http_demo_plaintext

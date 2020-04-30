CC = gcc

CSDK_ROOT = ../../..

INCLUDE_DIRS = -I . \
               -I $(CSDK_ROOT)/libraries/standard/http/include \
			   -I $(CSDK_ROOT)/platform/include

SRC_FILES = http_demo_plaintext.c \
			$(CSDK_ROOT)/platform/posix/iot_logging.o \
            $(shell find ../../../libraries/standard/http/src -name '*.c')

FLAGS = -g -O0 -Wall -Wextra -Wpedantic

all:
	$(CC) $(FLAGS) -lgcov $(INCLUDE_DIRS) $(SRC_FILES) -o http_demo_plaintext

clean:
	rm http_demo_plaintext

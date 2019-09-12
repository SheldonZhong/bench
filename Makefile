CFLAGS += -Wall -Wextra
LDLIBS += -lpthread

uring = -luring -L./liburing/src/ -I./liburing/src/include/

all: update devio liburing_devio

devio: devio.o common.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

liburing_devio: liburing_devio.c common.o
	$(CC) $(CFLAGS) -o $@ $^ $(uring)

update :
	git submodule init && git submodule update
	cd ./liburing/src && $(MAKE)

clean :
	$(RM) devio liburing_devio *.o

.PHONY: liburing update clean all

.PHONY: liburing update

objs = devio.o
flags = -lpthread -luring -L./liburing/src/ -I./liburing/src/include/

devio : devio.c
	cc -o devio.o devio.c $(flags)

uring : uring.c liburing
	cc -o uring.o uring.c $(flags)

liburing : update
	cd ./liburing/src && $(MAKE)

update :
	git submodule init && git submodule update

clean :
	rm *.o

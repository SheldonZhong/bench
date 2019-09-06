.PHONY: liburing update

objs = common.o
flags = -lpthread -luring -L./liburing/src/ -I./liburing/src/include/

devio : devio.c $(objs)
	cc -o devio.o devio.c $(objs) $(flags)

uring : uring.c liburing $(objs)
	cc -o uring.o uring.c $(objs) $(flags)

liburing : update
	cd ./liburing/src && $(MAKE)

common.o : common.h common.c
	cc -c common.c

update :
	git submodule init && git submodule update

clean :
	rm *.o

#
# Tue Dec 20 18:50:27 CST 2016
#
cflag := -ldb

all: test get put del walk puts

test: bdb_test.c
	cc -o $@ $(cflag)  $<
put: bdb_put.c
	cc -o $@ $(cflag)  $<
get: bdb_get.c
	cc -o $@ $(cflag)  $<
del: bdb_del.c
	cc -o $@ $(cflag)  $<
walk: bdb_walk.c
	cc -o $@ $(cflag)  $<
puts: bdb_puts.c
	cc -o $@ $(cflag)  $<

.PHONY: clean

clean:
	rm -f *.o test put get del walk puts


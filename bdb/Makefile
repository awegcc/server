#
# Tue Dec 20 18:50:27 CST 2016
#
all: test get put del
cflag := -ldb

test: bdb_test.c
	cc -o $@ $(cflag)  $<
put: bdb_put.c
	cc -o $@ $(cflag)  $<
get: bdb_get.c
	cc -o $@ $(cflag)  $<
del: bdb_del.c
	cc -o $@ $(cflag)  $<

.PHONY: clean

clean:
	rm -f *.o test put get del


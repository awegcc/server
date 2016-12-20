#
# Tue Dec 20 18:50:27 CST 2016
#
all: serv portRelay

serv: serv.c
	cc -o serv serv.c

portRelay: portRelay.c
	cc -o portRelay portRelay.c

.PHONY: clean

clean:
	rm -f *.o serv portRelay


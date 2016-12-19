
all: serv, portRelay

serv:
	cc -o serv serv.c

portRelay:
	cc -o portRelay portRelay.c

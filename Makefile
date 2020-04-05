CFLAGS = -Wall -g
LDLIBS = -lssl

all: random wiki url

random: random.o http.o
random.o: random.c http.h

wiki: wiki.o http.o
wiki.o: wiki.c http.h

url: url.o http.o
url.o: url.c http.h

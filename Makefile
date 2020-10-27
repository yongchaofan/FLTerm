#Makefile for Linux build with mbedTLS crypto backend
HEADERS = src/host.h src/ssh2.h
OBJS = obj/tiny2.o obj/ssh2.o obj/host.o obj/Fl_Term.o obj/Fl_Browser_Input.o

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags} -I.
LDFLAGS = ${shell fltk-config --ldstaticflags} -lstdc++ -lssh2 -lmbedcrypto

all: tinyTerm2 

tinyTerm2: ${OBJS} 
	cc -o "$@" ${OBJS} ${LDFLAGS}

obj/%.o: src/%.cxx ${HEADERS}
	${CC} ${CFLAGS} -c $< -o $@

clean:
	rm obj/*.o "tinyTerm2"

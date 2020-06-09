HEADERS = src/host.h src/ssh2.h 

TERM_OBJS=tiny2.o Fl_Term.o Fl_Browser_Input.o host.o ssh2.o
LIBS =  -lssh2 -lmbedcrypto
#/usr/local/lib/libssh2.a /usr/local/lib/libmbedcrypto.a
INCLUDE = -I.

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags} -lstdc++ -ldl -lpthread

all: tinyTerm2

tinyTerm2: ${TERM_OBJS} 
	cc -o "$@" ${TERM_OBJS} ${LDFLAGS} ${LIBS}

%.o: src/%.cxx ${HEADERS}
	${CC} ${CFLAGS} ${INCLUDE} -c $< -o $@

clean:
	rm *.o "tinyTerm2"

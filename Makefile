HEADERS = Fl_Term.h Hosts.h Nodes.h
LIBS =  /usr/local/lib/libssh2.a -L/usr/local/lib
INCLUDE = -I.

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags} -lc++ -lz -lssl -lcrypto

all: flTerm

flTerm: obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o obj/daemon.o
	cc -o "$@" obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o obj/daemon.o ${LIBS} ${LDFLAGS}

obj/%.o: src/%.cxx 
	${CC} ${CFLAGS} ${INCLUDE} -c $< -o $@
	
clean:
	rm obj/*.o "flTerm"

HEADERS = Fl_Term.h acInput.h Hosts.h
LIBS =  /usr/local/lib/libssh2.a

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags} -lc++ -lz -lssl -lcrypto

all: flTerm

flTerm: obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o
	cc -o "$@" obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o ${LIBS} ${LDFLAGS}

obj/%.o: src/%.cxx 
	${CC} ${CFLAGS} -c $< -o $@
	
clean:
	rm obj/*.o "flTerm"

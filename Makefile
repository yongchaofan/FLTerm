HEADERS = Fl_Term.h Fl_Host.h ssh2.h acInput.h
LIBS =  /usr/local/lib/libssh2.a -L/usr/local/lib
INCLUDE = -I.

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags} -lc++ -lz -lssl -lcrypto

all: flTerm

flTerm: obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Fl_Host.o obj/ssh2.o
	cc -o "$@" obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Fl_Host.o obj/ssh2.o ${LIBS} ${LDFLAGS}

obj/%.o: src/%.cxx 
	${CC} ${CFLAGS} ${INCLUDE} -c $< -o $@
	
clean:
	rm obj/*.o "flTerm"

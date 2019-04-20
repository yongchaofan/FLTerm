HEADERS = Fl_Term.h host.h ssh2.h
LIBS =  /usr/local/lib/libssh2.a

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags} -lc++ -lssl -lcrypto

all: tinyTerm2

tinyTerm2: obj/flTerm.o obj/Fl_Term.o obj/Fl_Browser_Input.o obj/host.o obj/ssh2.o
	cc -o "$@" obj/flTerm.o obj/Fl_Term.o obj/Fl_Browser_Input.o obj/host.o obj/ssh2.o ${LIBS} ${LDFLAGS}

obj/%.o: src/%.cxx 
	${CC} ${CFLAGS} -c $< -o $@
	
clean:
	rm obj/*.o "tinyTerm2"

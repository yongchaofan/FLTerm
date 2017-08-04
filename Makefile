HEADERS = Fl_Term.h ssh_Host.h
LIBS =  /mingw64/lib/libssh2.a /mingw64/lib/libz.a
INCLUDE_DIRS = -I../sqlite3 -I../tinyxml2

CFLAGS = -Os $(shell fltk-config --cxxflags)
LDFLAGS = -s $(shell fltk-config --ldstaticflags) -lws2_32 -lcrypt32 -lbcrypt -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread
RC = windres

all: flTerm.exe

flTerm.exe: obj/flTerm.o obj/Fl_Term.o obj/ssh_Host.o obj/resource.o
	cc -Wall -o "$@" obj/flTerm.o obj/Fl_Term.o obj/ssh_Host.o obj/resource.o ${LIBS} ${LDFLAGS}

obj/%.o: src/%.cxx ${HEADERS}
	${CC} ${CFLAGS} ${INCLUDE_DIRS} -c $< -o $@

obj/resource.o: res\fltk.rc res\fltk.manifest res\Fl.ico 
	${RC} -I. -I.\res -i $< -o $@

clean:
	rm obj/*.o "flTerm.exe"

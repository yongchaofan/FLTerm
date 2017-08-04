HEADERS = src/table.h src/Fl_Term.h src/ssh_Host.h src/ietf.h
OBJS =	obj/flTable.o obj/table.o obj/Fl_Term.o obj/ssh_Host.o obj/ietf.o obj/finity.o obj/resource.o ../sqlite3/sqlite3.o ../tinyxml2/tinyxml2.o 
LIBS =  /mingw64/lib/libssh2.a /mingw64/lib/libz.a
INCLUDE_DIRS = -I../sqlite3 -I../tinyxml2

CFLAGS = -Os $(shell fltk-config --cxxflags)
LDFLAGS = -s $(shell fltk-config --ldstaticflags) -lws2_32 -lcrypt32 -lbcrypt -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lpthread
RC = windres

all: flTable.exe flTerm.exe

flTable.exe: ${OBJS} 
	cc -Wall -o "$@" ${OBJS} ${LIBS} ${LDFLAGS}
flTerm.exe: obj/flTerm.o obj/Fl_Term.o obj/ssh_Host.o obj/resource.o
	cc -Wall -o "$@" obj/flTerm.o obj/Fl_Term.o obj/ssh_Host.o obj/resource.o ${LIBS} ${LDFLAGS}

obj/%.o: src/%.cxx ${HEADERS}
	${CC} ${CFLAGS} ${INCLUDE_DIRS} -c $< -o $@

obj/resource.o: res\fltk.rc res\fltk.manifest res\Fl.ico 
	${RC} -I. -I.\res -i $< -o $@

clean:
	rm obj/*.o "flTable.exe" "flTerm.exe"
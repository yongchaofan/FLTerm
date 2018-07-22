HEADERS = Fl_Term.h Hosts.h sftp.h acInput.h
LIBS =  /usr/local/lib/libssh2.a -L/usr/local/lib

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags} -lc++ -lz -lssl -lcrypto

all: flTerm

flTerm: obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o obj/sftp.o
	cc -o "$@" obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o obj/ftp.o ${LIBS} ${LDFLAGS}

obj/%.o: src/%.cxx 
	${CC} ${CFLAGS} -c $< -o $@
	
clean:
	rm obj/*.o "flTerm"

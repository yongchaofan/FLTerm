HEADERS = sqlTable.h Fl_Term.h Hosts.h Nodes.h
OBJS =	obj/flTable.o obj/sqlTable.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o obj/Nodes.o obj/finity.o obj/sqlite3.o obj/tinyxml2.o
LIBS =  /usr/local/lib/libssh2.a -L/usr/local/lib
INCLUDE = -I.

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags} -lc++ -lz -lssl -lcrypto

all: flTerm flTable

flTable: ${OBJS} 
	cc -o "$@" ${OBJS} ${LIBS} ${LDFLAGS}
flTerm: obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o obj/daemon.o
	cc -o "$@" obj/flTerm.o obj/Fl_Term.o obj/acInput.o obj/Hosts.o obj/daemon.o ${LIBS} ${LDFLAGS}

obj/%.o: src/%.cxx 
	${CC} ${CFLAGS} ${INCLUDE} -c $< -o $@

obj/tinyxml2.o: tinyxml2.cpp
	${CC} ${CFLAGS} ${INCLUDE} -c $< -o $@

obj/sqlite3.o: sqlite3.c
	${CC} ${INCLUDE} -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_PROGRESS_CALLBACK -DSQLITE_OMIT_SHARED_CACHE -c $< -o $@

clean:
	rm obj/*.o "flTable" "flTerm"

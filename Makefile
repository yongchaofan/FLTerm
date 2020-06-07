#Makefile for MSYS2/MingW64/MacOS/Linux
HEADERS = Fl_Term.h host.h ssh2.h
LIBS = -lssh2 -lmbedcrypto
obj_dir = obj

CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags}

all: tinyTerm2

tinyTerm2: obj_dir $(obj_dir)/tiny2.o $(obj_dir)/Fl_Term.o $(obj_dir)/Fl_Browser_Input.o $(obj_dir)/host.o $(obj_dir)/ssh2.o
	${CXX} -g -o "$@" $(obj_dir)/tiny2.o $(obj_dir)/Fl_Term.o $(obj_dir)/Fl_Browser_Input.o $(obj_dir)/host.o $(obj_dir)/ssh2.o ${LIBS} ${LDFLAGS}

$(obj_dir)/%.o: src/%.cxx
	${CXX} ${CFLAGS} -c $< -o $@

obj_dir:
	test ! -d $(obj_dir) && mkdir $(obj_dir)

clean:
	rm $(obj_dir)/*.o "tinyTerm2"

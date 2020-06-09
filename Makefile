#Makefile for macOS/Linux
HEADERS = src/host.h src/ssh2.h 
LIBS =  -lssh2 -lmbedcrypto
OBJ_DIR = obj
OBJS = $(OBJ_DIR)/tiny2.o $(OBJ_DIR)/ssh2.o $(OBJ_DIR)/host.o\
		$(OBJ_DIR)/Fl_Term.o $(OBJ_DIR)/Fl_Browser_Input.o
		
CFLAGS= -Os -std=c++11 ${shell fltk-config --cxxflags}
LDFLAGS = ${shell fltk-config --ldstaticflags} -lstdc++

all: OBJ_DIR tinyTerm2 

tinyTerm2: ${OBJS} 
	cc -o "$@" ${OBJS} ${LDFLAGS} ${LIBS}

$(OBJ_DIR)/%.o: src/%.cxx ${HEADERS}
	${CC} ${CFLAGS} -I. -c $< -o $@

OBJ_DIR:
	test ! -d $(OBJ_DIR) && mkdir $(OBJ_DIR)

clean:
	rm *.o "tinyTerm2"

# tinyTerm2
tinyTerm2 is a rewrite of tinyTerm in C++ with FLTK and libssh2, resulting a cross platform terminal emulator

source files included:

    Fl_Browser_Input.h and Fl_Browser_Input.cxx extends Fl_Input with autocompletion

    Fl_Term.h and Fl_Term.cxx implements a vt100 terminal widget using FLTK

    ssh2.h and ssh2.cxx implements ssh, sftp and netconf hosts

    host.h and host.cxx implements telnet and serial hosts, plus ftp and tftp daemon for win32

    tiny2.cxx combines Fl_Term, host and ssh2 to create the simple terminal application
  
tinyTerm2 can be compiled on Windows using mingW or Visual Studio command line tools, on MacOS using command line development tool or on UNIX/Linux using gnu tools

The Makefiles provided assumes that FLTK and libssh2 are installed in /usr/local. MacOS and Linux Makefile are the same, mingW64 Makefile is a little different in that it uses Windows native crypto library(WinCNG) and includes a resource file to provide file version information and application icon. 

precompiled binaries are located in build directory.

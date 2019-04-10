# flTerm
ssh terminal based on FLTK 1.34.0 and libssh2 1.9.0, built for network engineers with the emphasis on scriptability

source files included:

    Fl_Browser_Input.h and Fl_Browser_Input.cxx extends Fl_Input with autocompletion

    Fl_Term.h and Fl_Term.cxx implements a vt100 terminal widget using FLTK

    ssh2.h and ssh2.cxx implements ssh, sftp and netconf hosts

    Hosts.h and Hosts.cxx implements telnet and serial hosts

    flTerm.cxx combines Fl_Term, Hosts and sftp to create the simple terminal application
  
flTerm can be compiled on Windows using mingW or Visual Studio command line tools, on MacOS using command line development tool and on Linux using gcc

The Makefiles provided assumes that FLTK and libssh2 are installed in /usr/local. MacOS and Linux Makefile are the same, mingW64 Makefile is a little different in that it uses Windows native crypto library(WinCNG) and includes a resource file to provide file version information and application icon. 

precompiled binaries are located in build directory.

# flTerm
ssh terminal based on FLTK 1.3.4 and libssh2 1.8.0, built for network engineers with the emphasis on scriptability

source files included:

    Fl_Term.h and Fl_Term.cxx implements a vt100 terminal widget using FLTK
  
    Hosts.h and Hosts.cxx implements telnet, ssh and netconf hosts 
  
    sftp.h and sftp.cxx implements sftp client host
  
    flTerm.cxx combines Fl_Term, Hosts and sftp to create the simple terminal application
  
flTerm can be compiled on Windows using mingW, on MacOS using command line development tool and on Linux using gcc

The Makefiles provided assumes that FLTK and libssh2 are installed in /usr/local. MacOS and Linux Makefile are the same, mingW64 Makefile is a little different in that it uses Windows native crypto library(WinCNG) and includes a resource file to provide file version information and application icon. 

precompiled binaries are located in build directory.

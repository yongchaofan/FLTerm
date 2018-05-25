# flTerm
ssh terminal based on FLTK 1.3.4 and libssh2 1.8.0, built for network engineers with the emphasis on scriptability

source files included:

  Fl_Term.h and Fl_Term.cxx implements a vt100 terminal widget using FLTK
  
  ssh_Host.h and ssh_Host.cxx implements telnet and ssh protocol using libssh2
  
  flTerm.cxx combines ssh_Host and flTerm to create the simple terminal application
  
flTerm can be compiled on Windows using mingW, on MacOS using command line development tool and Linux using gcc

The Makefiles provided assumes that FLTK and libssh2 are installed in /usr/local. MacOS and Linux Makefile are the same, mingW64 Makefile is a little different in that it tries to use libcrypto the Windows native crypto library, mingW32 Makefile use openssl crypto library like the Linux verion, both mingW Makefiles also includes a resource file to provide file version information and application icon.

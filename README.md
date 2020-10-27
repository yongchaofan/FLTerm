# [FLTerm](http://yongchaofan.github.io/FLTerm)

[![Build Status](https://travis-ci.org/pages-themes/minimal.svg?branch=master)](https://travis-ci.org/pages-themes/minimal) 

*Minimalist terminal emulator, designed by network engineer for network engineers, with unique features for effeciency and effectiveness when managing network devices like routers, switches, transponders and ROADMs through command line interface.*

![Thumbnail of minimal](docs/FLTerm-0.png)


## Project philosophy

FLTerm(Fast Light Terminal, formally "tinyTerm2") is a rewrite of tinyTerm in C++ with FLTK and libssh2+mbedtls, resulting a cross platform terminal emulator, with multi-tab support, and continues to be small, simple and scriptable. win64 executable is 824KB, macOS and linux executables are just above 1MB.

User interface design is minimal, program starts with no tabs, tabs are enabled automatically when second connection is made; scrollbar hidden until user trys to scroll back, only one dialog for makeing connections, 
    
### Librarys

    libssh2 1.9.0 for full support of WinCNG crypto functions
            ./configure --with-crypto=wincng --without-libz
            make install
            
    mbedTLS crypto library is used on WindowsXP, download mbedtls-2.16.1, 
            "make no_test install"          //add WINDOWS_BUILD=1 on windows 
            then build libssh2
            ./configure --with-crypto=mbedtls --without-libz
            make install
            
### source files included:

    ssh2.h and ssh2.cxx implements ssh/netconf and sftp hosts

    host.h and host.cxx implements telnet, serial and shell host
    
    Fl_Term.h and Fl_Term.cxx implements a vt100 terminal widget using FLTK

    Fl_Browser_Input.h and Fl_Browser_Input.cxx extends Fl_Input with autocompletion

    tiny2.cxx combines Fl_Term, host and ssh2 to create the simple terminal application  


## Building
FLTerm can be compiled on Windows using mingW or Visual Studio command line tools, on MacOS using command line development tool or on UNIX/Linux using gnu tools. Makefiles are provided for building with MSYS2+MingW64/32, also a cmd file for building with Visual Studio building tools.

    Makefile        building with mbedTLS crypto backend on macOS/Linux or on windows using mingw64
    Makefile.macos  building with openssl crypto backend on macOS using Xcode command line tools and gmake
    Makefile.nmake  building with wincng crypto backend on Windows using Visual Studio command line tools and nmake

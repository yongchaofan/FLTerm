rc res\tiny2.rc

cl /c -Ox /MD /DWIN32 /I../%Platform%/include src/tiny2.cxx src/Fl_Term.cxx src/host.cxx src/ssh2.cxx src/Fl_Browser_Input.cxx 

link /SUBSYSTEM:WINDOWS tiny2.obj Fl_Term.obj Fl_Browser_Input.obj host.obj ssh2.obj res\tinyTerm2.res user32.lib gdi32.lib comdlg32.lib comctl32.lib ole32.lib shell32.lib ws2_32.lib uuid.lib bcrypt.lib crypt32.lib shlwapi.lib Advapi32.lib ../%Platform%/lib/libssh2.lib ../%Platform%/lib/fltk.lib /out:tinyTerm2_%Platform%.exe

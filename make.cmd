rc res\tinyTerm2.rc

cl /c -O1 /GL /DWIN32 /I../%Platform%/include %1 %2 %3 %4 %5 

link /LTCG /NXCOMPAT /DYNAMICBASE /NODEFAULTLIB:libucrt.lib ucrt.lib /SUBSYSTEM:WINDOWS tiny2.obj Fl_Term.obj Fl_Browser_Input.obj host.obj ssh2.obj res\tinyTerm2.res user32.lib gdi32.lib comdlg32.lib comctl32.lib ole32.lib shell32.lib ws2_32.lib uuid.lib bcrypt.lib crypt32.lib shlwapi.lib Advapi32.lib ../%Platform%/lib/libssh2.lib ../%Platform%/lib/fltk.lib /out:tinyTerm2_%Platform%.exe

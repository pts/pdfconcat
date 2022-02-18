This is the official repository for pdfconcat, a simple PDF document
concatenator written in standard C (C89).

pdfconcat: simple PDF document concatenator written in C89

pdfconcat is a small and fast command-line utility written in C89 (ANSI C)
that can concatenate (merge) several PDF files into a long PDF document.
External libraries are not required, only ANSI C functions are used.
Several features of the output file are taken from the first input file
only. For example, outlines (also known as hierarchical bookmarks) in
subsequent input files are ignored. pdfconcat compresses its input a
little bit by removing whitespace and unused file parts.

pdfconcat has been tested on various huge PDFs downloaded from the
Adobe web site, plus an 1200-pages long mathematics manual typeset by
LaTeX, emitted by pdflatex, dvipdfm and `gs -sDEVICE=pdfwrite', totalling
5981 pages in a single PDF file.

Usage:

  $ ./pdfconcat -o output.pdf in1.pdf in2.pdf in3.pdf

Features:

* uses few memory (only the xref table is loaded into memory)
* is fast, because of the low level ANSI C usage
* compresses input PDFs by removing whitespace and unused objects

Limitations:

* does not support cross-reference streams and objects streams in the
  input PDF
* keeps outlines (bookmarks, hierarchical table of contents) of only the
  first PDF (!)
* doesn't work if the input PDFs have different encryption keys
* result is undefined when there are hyperlink naming conflicts
* detects the binaryness of only the first input PDF
* cannot verify and/or ensure copyright of PDF documents
* emits various error messages, but it isn't a PDF validator
* /Linearized property is destroyed

Because of the limitations of pdfconcat above, it's recommended to use qpdf
instead of pdfconcat to concatenate arbitrary PDF files:

  $ qpdf --empty --pages in1.pdf 1-z in2.pdf 1-z in3.pdf 1-z -- output.pdf

As an older alternative of qpdf, it's possible to use pdftk to concatenate
PDF files:

  $ pdftk in1.pdf in2.pdf in3.pdf cat output output.pdf

System requirements:

* Compiles and works on any Unix system and on Windows i386 and amd64 if
  compiled with MinGW32 (also when cross-compiled on Linux with
  i586-mingw32msvc-gcc).

Programming language compatibility of pdfconcat.c:

* C: C89 (ANSI C), C99 and C11.
* C++: C++98, C++11, C++14, C++17, C++20.

Compiler compatibilty:

* It should compile successfully with any C and C++ compiler. Tested with
  GCC 4.8, 6.3, 7.5 and 11.2 and Clang 3.8 as both C and C++. Also tested
  with tcc targeting i386.
* It should compile without warnings with GCC, Clang and tcc.
* It should work with any C library. Tested with glibc, uClibc and
  msvcrt.dll.
* Example compiler command-lines:

    $ gcc                -O3 -s -DNDEBUG=1           -Wunused -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ gcc     -std=c89   -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ gcc     -std=c99   -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ gcc     -std=c11   -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ clang   -std=c11   -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ tcc                -O3 -s -DNDEBUG=1           -Wunused -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ pts-tcc            -O3 -s -DNDEBUG=1           -Wunused -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ g++     -std=c++98 -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Winline -Wpointer-arith -Wcast-qual -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ g++     -std=c++11 -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Winline -Wpointer-arith -Wcast-qual -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ g++     -std=c++14 -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Winline -Wpointer-arith -Wcast-qual -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ g++     -std=c++17 -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Winline -Wpointer-arith -Wcast-qual -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ g++     -std=c++20 -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Winline -Wpointer-arith -Wcast-qual -Wmissing-declarations pdfconcat.c -o pdfconcat
    $ clang++ -std=c++14 -O3 -s -DNDEBUG=1 -pedantic -Wunused -Wall -W -Wstrict-prototypes -Wnested-externs -Winline -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes -Wmissing-declarations pdfconcat.c -o pdfconcat

The license of pdfconcat is GPL v2 or later:

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

__END__

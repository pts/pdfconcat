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

System requirements:

* Compiles and works on any Unix system and on MinGW32 (also when
  cross-compiled on Linux with i586-mingw32msvc-gcc).

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

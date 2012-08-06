#define DUMMY /* \
  set -ex; \
  CFLAGS="-O3 -s -DNDEBUG=1";  [ "$1" ] && CFLAGS="-g"; \
  CC=gcc; [ _"$1" = _-c ] && CC=checkergcc; \
  $CC $CFLAGS -DNO_CONFIG=1 -ansi -pedantic -Wunused \
    -Wall -W -Wstrict-prototypes -Wnested-externs -Winline \
    -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wmissing-prototypes \
    -Wmissing-declarations "$0" -o pdfconcat; \
  exit
*/
/* pdfconcat.c -- ANSI C program to concatenate PDF files
 * by pts@fazekas.hu at Sat Nov  1 10:19:37 CET 2003
 * -- Sun Nov  2 00:30:25 CET 2003
 *
 * pdfconcat is a small and fast command-line utility written in ANSI C that
 * can concatenate (merge) several PDF files into a long PDF document.
 * External libraries are not required, only ANSI C functions are used.
 * Several features of the output file are taken from the first input file
 * only. For example, outlines (also known as hierarchical bookmarks) in
 * subsequent input files are ignored. pdfconcat compresses its input a
 * little bit by removing whitespace and unused file parts.
 *
 * This program has been tested on various huge PDFs downloaded from the
 * Adobe web site, plus an 1200-pages long mathematics manual typeset by
 * LaTeX, emitted by pdflatex, dvipdfm and `gs -sDEVICE=pdfwrite', totalling
 * 5981 pages in a single PDF file.
 *
 * Features:
 *
 * -- uses few memory (only the xref table is loaded into memory)
 * -- is fast, because of the low level ANSI C usage
 * -- compresses input PDFs by removing whitespace and unused objects
 *
 * Limitations:
 *
 * -- does not support cross-reference streams and objects streams in the
 *    input PDF
 * -- keeps outlines (bookmarks, hierarchical table of contents) of only the
 *    first PDF (!!)
 * -- doesn't work if the input PDFs have different encryption keys
 * -- result is undefined when there are hyperlink naming conflicts
 * -- detects the binaryness of only the first input PDF
 * -- cannot verify and/or ensure copyright of PDF documents
 * -- emits various error messages, but it isn't a PDF validator
 * -- /Linearized property is destroyed
 */

/*
 * Imp: optional safe mode, emitting more '\0'
 * Imp: true generation handling
 * Imp: extensive documentation
 * Dat: output must be seekable, so it cannot be a pipe
 * Dat: ungetc() destroys value of ftell(), even after getc()...
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h> /* exit() */
#include <errno.h> /* errno */

#if NO_CONFIG
  #include <assert.h>
  #if SIZEOF_INT>=4
    typedef unsigned slen_t;
    typedef int slendiff_t;
  #  define SLEN_P ""
  #else
    typedef unsigned long slen_t;
    typedef long slendiff_t;
  #  define SLEN_P "l"
  #endif
  typedef char sbool;
  #if ';'!=59 || 'a'!=97
  #  error ASCII system is required to compile this program
  #endif
#else
  #include "config2.h" /* by sam2p... */
  typedef bool sbool;
#endif
#if OBJDEP
#  warning PROVIDES: pdfconcat_main
#endif

#undef  TRUE
#define TRUE 1
#undef  FALSE
#define FALSE 0

#ifdef NDEBUG
#  define ASSERT_SE(x,y) (y) /* assert with side effect */
#else
#  define ASSERT_SE(x,y) assert(x(y))
#endif

#define ULE(a,b) (((a)+0U)<=((b)+0U))
#define ISWSPACE(i,j) ((i j)==32 || ULE(i-9,13-9) || i==0)

#define PROGNAME "pdfconcat"
#define VERSION "0.02"

/* --- Data */

#if 0
#define SBUFSIZE 4096

/** Data area holding strings; */
char sbuf[SBUFSIZE], *sbufb;

#define WORDSSIZE 127

/** Pointers inside sbuf, indexed by chars, [0..31] are special */
char const* words[WORDSSIZE];

static void sbuff(void) {
  unsigned i;
  sbuf[0]='\0';
  sbufb=sbuf+1;
  for (i=0;i<WORDSSIZE;i++) words[i]=sbuf;
}

/** @return -1 or the word found */
static int findword(char const*word) {
  unsigned i=32;
  while (i<WORDSSIZE && 0!=strcmp(word,words[i])) i++;
  return i==WORDSSIZE ? -1 : (int)i;
}

/** @return -1 or the word found */
static int findword_stripslash(char const*word) {
  unsigned i=32;
  assert(word[0]!='/');
  while (i<WORDSSIZE && 0!=strcmp(word,words[i]+(words[i][0]=='/'))) i++;
  return i==WORDSSIZE ? -1 : (int)i;
}
#endif

/* --- Reading */

struct XrefEntry {
  slen_t ofs;
  unsigned short gennum;
  char type; /* 'n' or 'f' */
  
  slen_t target_num; /* 0: not reached yet */
  struct XrefEntry *next;
};

static struct ReadState {
  FILE *file;
  char const* filename;
  slen_t filesize;
  struct XrefEntry *xrefs;
  slen_t xrefc;
  slen_t lastofs; /* set by gettok() for 'E', '.', '1' or 'b' etc. */
  slen_t catalogofs;
  slen_t uppagesofs;
  slen_t pagecount;
  slen_t xreftc; /* number of xref tables -- for debugging */
  slen_t trailer1ofs;
  sbool is_binary;
  char pdf_header[10];
} currs;

static void erri(char const*msg1, char const*msg2) {
  fflush(stdout);
  fprintf(stderr, "%s: error at %s:%"SLEN_P"u: %s%s\n",
    PROGNAME, currs.filename, (slen_t)ftell(currs.file), msg1, msg2?msg2:"");
  exit(3);
}
static void errn(char const*msg1, char const*msg2) {
  fflush(stdout);
  fprintf(stderr, "%s: error: %s%s\n",
    PROGNAME, msg1, msg2?msg2:"");
  exit(3);
}

/** Also limits maximum string length */
#define IBUFSIZE 32768

/** Input buffer for several operations. */
char ibuf[IBUFSIZE];
/** Position after last valid char in ibuf */
char *ibufb;

typedef slendiff_t pdfint_t;

pdfint_t ibuf_int;

static /*inline*/ sbool is_ps_white(int/*char*/ c) {
  return c=='\n' || c=='\r' || c=='\t' || c==' ' || c=='\f' || c=='\0';
}

static /*inline*/ sbool is_ps_name(int/*char*/ c) {
  /* Dat: we differ from PDF since we do not treat the hashmark (`#') special
   *      in names.
   * Dat: we differ from PostScript since we accept names =~ /[!-~]/
   */
  return c>='!' && c<='~'
      && c!='/' && c!='%' && c!='{' && c!='}' && c!='<' && c!='>'
      && c!='[' && c!=']' && c!='(' && c!=')';
  /* Dat: PS avoids: /{}<>()[]% \n\r\t\000\f\040 */
}

#if 0
/** Definition chosen rather arbitrarily by pts */
static sbool is_wordx(char const *s) {
  if (!ULE(*s-'A','Z'-'A') && !ULE(*s-'a','z'-'a') && *s!='.') return 0;
   /* && !ULE(*s-'0','9'-'0') && *s!='-') return 0; */
  while (*++s!='\0') if (!is_ps_name(*s)) return 0;
  return 1;
}
#endif

/** @param b: assume null-terminated @return true on error */
static /*inline*/ sbool toInteger(char *s, pdfint_t *ret) {
  /* Dat: for both toInteger() and PDF `-5' and `+5' is OK, `--5' isn't */
  int n=0; /* BUGFIX?? found by __CHECKER__ */
  return sscanf(s, "%"SLEN_P"i%n", ret, &n)<1 || s[n]!='\0';
}

/** @param b: assume null-terminated @return true on error */
static /*inline*/ sbool toReal(char *s, double *ret) {
  int n;
  char c;
  /* Dat: glibc accepts "12e", "12E", "12e+" and "12E-" */
  return sscanf(s, "%lf%n", ret, &n)<1
      || (c=s[n-1])=='e' || c=='E' || c=='+' || c=='-' || s[n]!='\0';
}

static void r_seek(slen_t begofs) {
  if (0!=fseek(currs.file, begofs, SEEK_SET)) {
    fprintf(stderr, "%s: unseekable %s: %s\n", PROGNAME, currs.filename, strerror(errno));
    exit(6);
  }
}

/** Returns a PostScript token ID, puts token into buf */
static char gettok(void) {
  /* Derived from MiniPS::Tokenizer::yylex() of sam2p-0.37 */
  int c=0, d; /* dummy initialization */
  sbool hi;
  unsigned hv=0; /* =0: pacify G++ 2.91 */
  slen_t nest;
  char *ibufend=ibuf+IBUFSIZE;
  ibufb=ibuf;

#if 0  
  if (ungot==EOFF) return EOFF;
  if (ungot!=NO_UNGOT) { c=ungot; ungot=NO_UNGOT; goto again; }
#endif
 again_getcc:
  c=getc(currs.file);
 /* again: */
  switch (c) {
   case -1: eof:
    return 0; /*ungot=EOFF */;
   case '\n': case '\r': case '\t': case ' ': case '\f': case '\0':
    goto again_getcc;
   case '%': /* one-line comment */
#if 0 /* XMLish tag from ps_tiny.c */
    if ((c=getc(currs.file))=='<') {
      char ret='<';
      if ((c=getc(currs.file))=='/') { ret='>'; c=getc(currs.file); } /* close tag */
      if (!ULE(c-'A','Z'-'A')) erri("invalid tag",0); /* catch EOF */
      (ibufb=ibuf)[0]=c; ibufb++;
      while (ULE((c=getc(currs.file))-'A','Z'-'A') || ULE(c-'a','z'-'a')) {
        if (ibufb==ibufend-1) erri("tag too long",0);
        *ibufb++=c;
      }
      if (c<0) erri("unfinished tag",0);
      *ibufb='\0';
      ungetc(c,currs.file);
      return ret;
    }
#endif
    while (c!='\n' && c!='\r' && c!=-1) c=getc(currs.file);
    if (c==-1) goto eof;
    goto again_getcc;
   case '[':
    *ibufb++=c;
    return '[';
   case ']':
    *ibufb++=c;
    return ']';
   case '{': case '}':
    erri("proc arrays disallowed",0);
    /* Dat: allowed in PS, but not in PDF */
   case '>':
    if (getc(currs.file)!='>') goto err;
    *ibufb++='>'; *ibufb++='>';
    return '>';
   case '<':
    if ((c=getc(currs.file))==-1) { uf_hex: erri("unfinished hexstr",0); }
    if (c=='<') {
      *ibufb++='<'; *ibufb++='<';
      return '<';
    }
    if (c=='~') erri("a85str disallowed",0); /* allowed in PS, but not in PDF */
    hi=1;
    while (c!='>') {
           if (ULE(c-'0','9'-'0')) hv=c-'0';
      else if (ULE(c-'a','f'-'a')) hv=c-'a'+10;
      else if (ULE(c-'A','F'-'A')) hv=c-'A'+10;
      else if (is_ps_white(c)) hv=16;
      else erri("syntax error in hexstr",0);
      if (hv==16) ;
      else if (!hi) { ibufb[-1]|=hv; hi=1; }
      else if (ibufb==ibufend) erri("hexstr literal too long",0);
      else { *ibufb++=(char)(hv<<4); hi=0; }
      if ((c=getc(currs.file))==-1) goto uf_hex;
    }
    /* This is correct even if an odd number of hex digits have arrived */
    return '(';
   case '(':
    nest=1;
    c=getc(currs.file);
    while (c!=-1) {
      if (c==')' && --nest==0) return '(';
      if (c=='\r') {
        if ((c=getc(currs.file))=='\n') {} /* convert "\r\n" -> "\n", as specified in subsection 3.2.3 of PDFRef.pdf */
        else { d='\n';
         dcont:
          if (ibufb==ibufend) erri("str literal too long",0);
          *ibufb++=d;
          continue;
        }
      } else if (c!='\\') { if (c=='(') nest++; }
      else switch (c=getc(currs.file)) { /* read a backslash escape */
       case -1: goto uf_str;
       case 'n': c='\n'; break;
       case 'r': c='\r'; break;
       case 't': c='\t'; break;
       case 'b': c='\010'; break; /* \b and \a conflict between -ansi and -traditional */
       case 'f': c='\f'; break;
       default:
        if (!ULE(c-'0','7'-'0')) break;
        hv=c-'0'; /* read at most 3 octal chars */
        if ((c=getc(currs.file))==-1) goto uf_str;
        if (c<'0' || c>'7') { d=hv; goto dcont; }
        else { hv=8*hv+(c-'0');
          if ((c=getc(currs.file))==-1) goto uf_str;
          if (c<'0' || c>'7') { d=hv; goto dcont; }
                         else c=(char)(8*hv+(c-'0'));
        }
      } /* SWITCH */
      if (ibufb==ibufend) erri("str literal too long",0);
      /* putchar(c); */
      *ibufb++=c;
      c=getc(currs.file);
    } /* WHILE */    
    /* if (c==')') return '('; */
    uf_str: erri("unfinished str",0);
   case ')': goto err;
   case '/':
    *ibufb++='/';
    while (ISWSPACE(c,=getc(currs.file))) ;
    /* ^^^ `/ x' are two token in PostScript, but here we overcome the C
     *     preprocessor's feature of including whitespace.
     */
    /* fall-through, b will begin with '/' */
   default: /* /nametype, /integertype or /realtype */
    *ibufb++=c;
    while ((c=getc(currs.file))!=-1 && is_ps_name(c)) {
      *ibufb++=c;
      if (ibufb==ibufend) erri("token too long",0);
    }
    *ibufb='\0'; /* ensure null-termination */
    currs.lastofs=ftell(currs.file)-1;
    r_seek(currs.lastofs); /* Dat: ungetc(c,currs.file) would destroy ftell() return value */
    if (ibuf[0]=='/') return '/';
    /* Imp: optimise numbers?? */
    if (ibufb!=ibufend) {
      double d;
      /* Dat: PDF doesn't support (but PS does) base-n number such as `16#100' == 256; nor exponential notation (6e7) */
      if (!toInteger(ibuf, &ibuf_int)) {
        sprintf(ibuf, "%"SLEN_P"d", ibuf_int); ibufb=ibuf+strlen(ibuf); /* compress it */
        return '1';
      }
      /* Dat: call toInteger _before_ toReal */
      if (!toReal(ibuf, &d)) { 
        /* Dat: `.5' and `6.' are valid PDF reals */
        char *p;
        p=ibuf; while (*p!='\0' && *p!='e' && *p!='E') p++;
        if (*p!='\0') erri("exponential notation disallowed in PDF",0); /* Imp: convert to a name token instead */
        p=ibufb=ibuf;
        while (*p=='0') p++; /* strip heading zeros */
        while (*p!='\0') *ibufb++=*p++;
        while (ibufb!=ibuf && ibufb[-1]=='0') ibufb--; /* strip trailing zeros */
        /* *ibufb='\0'; -- not required */
      }
    }
    switch (*ibuf) {
     case 'R': if (0==strcmp(ibuf,"R")) return 'R';
               break;
     case 't': if (0==strcmp(ibuf,"true")) return 'b';
               break;
     case 'f': if (0==strcmp(ibuf,"false")) return 'b';
               break;
     case 'n': if (0==strcmp(ibuf,"null")) return 'n';
               break;
    }
    return 'E'; /* -endstream obj endobj stream trailer xref startxref */
  }
 err:
  erri("syntax error, token expected",0);
  goto again_getcc; /* notreached */
}

static void r_check_pdf_header(void) {
  int c;
  r_seek(0);
  if (9>fread(ibuf, 1, 9, currs.file)
   || 0!=memcmp(ibuf, "%PDF-", 5)
   || !ULE(ibuf[5]-'0','9'-'0')
   || ibuf[6]!='.'
   || !ULE(ibuf[7]-'0','9'-'0')
   || !is_ps_white(ibuf[8])
     ) {
    erri("invalid PDF header", 0);
  }
  ibuf[8]='\n'; ibuf[9]='\0';
  memcpy(currs.pdf_header, ibuf, sizeof(currs.pdf_header));
  currs.is_binary=FALSE; /* Imp: should we count >=4 high chars? */
  r_seek(0);
  /* vvv Seek binary bytes in the first few comment lines, see subsection 3.4.1 in PDFRef.pdf */
  while (1) {
    while ((c=getc(currs.file))=='\n' || c=='\r') {}
    if (c!='%') break;
    while ((c=getc(currs.file))!='\n' && c!='\r' && c!=-1) if ((c&0x80)!=0) { currs.is_binary=TRUE; break; }
  }
}

/** Minimum offset in the PDF file that an object may start */
#define OBJ_MIN_OFS 9

static void r_seek_xref(void) {
  pdfint_t xrefofs;
  char *p;
  int n=0; /* BUGFIX?? found by __CHECKER__ */
  slen_t got;
  r_seek(currs.filesize > 256 ? currs.filesize-256 : 0);
  if (0==(got=fread(ibuf, 1, 256, currs.file))) erri("cannot read startxref",0);
  p=ibuf+got;
  while (p!=ibuf && (p[-1]!='s' ||
    1!=sscanf(p,"tartxref%"SLEN_P"i%n",&xrefofs,&n))) p--;
  if (p==ibuf) erri("cannot find startxref",0);
  p[n]='\0';
  if (xrefofs<OBJ_MIN_OFS) erri("invalid startxref num",p+8);
  #if DEBUG
    fprintf(stderr,"startxref=(%lu)\n",xrefofs);
  #endif
  r_seek(xrefofs);
}

/** Uses currs.xrefs, seeks past `X Y obj'. */
static struct XrefEntry *objentry(pdfint_t num, pdfint_t gennum) {
  struct XrefEntry *e;
  char const* emsg;
  if (num<0 || (slen_t)0+num>=currs.xrefc) {
    emsg="obj num out of bounds: ";
    err: { char tmp[64];
      sprintf(tmp, "%"SLEN_P"d %"SLEN_P"d obj", num, gennum);
      erri(emsg, tmp);
    }
  }
  if (gennum<0 || gennum+0U>=65535U) { emsg="obj gennum bounds: "; goto err; }
  #if DEBUG
    fprintf(stderr, "resolving num=%ld\n", num);
  #endif
  /* Dat: pdftex creates unused objects like `0000000083 00000 f '
   *      when certain fonts are not subsetted (e.g. `<<cmr10.pfb' in the
   *      .map file)
   */
  if ((e=currs.xrefs+num)->type!='n' && e->type!='f') { emsg="bad type for obj: "; goto err; }
  if (e->gennum!=gennum) { emsg="gennum mismatch: "; goto err; }
  return e;
}

static void r_seek_obj(pdfint_t num, pdfint_t gennum) {
  char const *emsg;
  struct XrefEntry *e=objentry(num, gennum);
  r_seek(e->ofs);
  if ('1'!=gettok() || ibuf_int!=num) { emsg="inobj num mismatch: ";
    err: { char tmp[64];
      sprintf(tmp, "%"SLEN_P"d %"SLEN_P"d obj", num, gennum);
      erri(emsg, tmp);
    }
  }
  if ('1'!=gettok() || ibuf_int!=gennum) { emsg="inobj gennum mismatch: "; goto err; }
  if ('E'!=gettok() || 0!=strcmp(ibuf,"obj")) { emsg="inobj `obj' missing: "; goto err; }
}

static sbool is_digits(char const *p, char const *pend) {
  while (p!=pend && ULE(*p-'0','9'-'0')) p++;
  return p==pend;
}

#if 0
static void getotag(char const*tag) {
#if 0 /* This code segment cannot ignore comments */
  char const *p=tag;
  int c;
  while (ISWSPACE(c,=getcc())) ;
  if (c!='%' || (c=getcc())!='<') erri("tag expected: ", tag);
  while (ISWSPACE(c,=getcc())) ;
  while (p[0]!='\0') {
    if (c!=*p++) erri("this tag expected: ", tag);
    c=getcc();
  }
  ungetc(c,currs.file);
#else
  if (gettok()!='<' || 0!=strcmp(ibuf,tag)) erri("tag expected: ", tag);
#endif
}

static void gettagbeg(void) {
  int c;
  while (ISWSPACE(c,=getcc())) ;
  if (c!='>') erri("`>' expected",0);
}

static void gettagend(void) {
  int c;
  while (ISWSPACE(c,=getcc())) ;
  if (c!='/') erri("`/>' expected",0);
  while (ISWSPACE(c,=getcc())) ;
  if (c!='>') erri("`/>' expected",0);
}

static void getkey(char const *key) {
  char const *p=key;
  int c;
  while (ISWSPACE(c,=getcc())) ;
  while (p[0]!='\0') {
    if (c!=*p++) erri("this key expected: ", key);
    c=getcc();
  }
  while (ISWSPACE(c,)) c=getcc();
  if (c!='=') erri("key `=' expected", 0);
}

/** Loads a value into ibuf, '\0'-terminated. */
static void getval(void) {
  sbool g=1;
  char *ibufend1=ibuf+IBUFSIZE-1, c;
  ibufb=ibuf;
  while (ISWSPACE(c,=getcc())) ;
  while (1) {
    if (c=='"') g=!g;
    else if (g && (ISWSPACE(c,) || c=='/' || c=='>')) { ungetcc(c); break; }
    else if (c<0) erri("unfinished tag val",0);
    else if (c==0) erri("\\0 disallowed in tag val",0);
    else if (ibufb==ibufend1) erri("tag val too long",0);
    else *ibufb++=c;
    c=getcc();
  } /* WHILE */
  *ibufb='\0';
}

static pdfint_t getuintval(void) {
  pdfint_t ret;
  getval();
  /* fprintf(stderr, "[%s]\n", ibuf); */
  if (toInteger(ibuf, &ret) || ret<0) erri("tag val must be nonnegative integer",0);
  return ret;
}
#endif

/* --- Writing */

/** Maximum number of characters in a line. */
#define MAXLINE 78

static struct WriteState {
  /** Number of characters already written into this line. (from 0) */
  slen_t colc;
  /** Last token was a self-closing one */
  sbool lastclosed, is_binary;
  FILE *wf;
  char const* filename;
  char* trailer;
  slen_t outobjc; /* # assigned objs */
  slen_t trailerlen;
  slen_t *txrefs; /* txrefs[I] is the target file offset for `I 0 obj' */
  slen_t txrefc; /* number of items used in txrefs */
  slen_t txrefa; /* number of items allocated in txrefs */
  slen_t startxrefofs;
  slen_t lastsrcpages_num;
  slen_t pagetotal;
  slen_t *srcpages_nums;
  slen_t srcpages_numc; /* number of subfiles */
} curws;

#if 0
static void init_out(void) { curws.wf=stdout; curws.colc=0; curws.lastclosed=TRUE; }
#endif

static void newline(void) {
  if (curws.colc!=0) {
    putc('\n',curws.wf);
    curws.colc=0; curws.lastclosed=TRUE;
  } else assert(curws.lastclosed);
}

/** @return the byte length of a string as a quoted PostScript ASCII string
 * literal
 */
static slen_t pstrqlen(register char const* p, char const* pend) {
  slen_t olen=2+pend-p; /* '(' and ')' */
  /* Close parens after this */
  char const *q=pend;
  /* Number of parens opened so far */
  slen_t nest=0;
  char c;
  p=ibuf; pend=ibufb;  while (p!=pend) {
    if ((c=*p++)=='\r') { olen++; continue; }
    else if (c=='(') {
      while (q>p && *--q!=')') {}
      if (q<=p) { olen++; continue; }
      nest++;
    } else if (c==')') {
      if (nest!=0) {
        assert(q!=pend);
        while (*q++!=')') {}
        nest--;
      } else { olen++; continue; }
    }
  }
  assert(nest==0);
  return olen;
}
/** Prints the specified string as a quoted PostScript ASCII string literal.
 * Does not modify curws.colc etc.
 */
static void pstrqput(register char const* p, char const* pend) {
  /* Close parens after this */
  char const *q=pend;
  /* Number of parens opened so far */
  slen_t nest=0;
  char c;
  putc('(',curws.wf); curws.colc++;
  p=ibuf; pend=ibufb;  while (p!=pend) {
    if ((c=*p++)=='\n') { putc('\n',curws.wf); curws.colc=0; continue; }
    else if (c=='\r' || c=='\\') { put2: putc('\\',curws.wf); putc(c,curws.wf); curws.colc+=2; continue; }
    else if (c=='(') {
      while (q>p && *--q!=')') {}
      if (q<=p) goto put2;
      nest++;
    } else if (c==')') {
      if (nest!=0) {
        assert(q!=pend);
        while (*q++!=')') {}
        nest--;
      } else goto put2;
    }
    putc(c,curws.wf); curws.colc++; continue;
#if 0
    else if (ULE(c-32, 126-32)) { putc(c); curws.colc++; continue; }
    curws.colc+=2;
    putc('\\');
         if (c=='\r')   putc('r');
#if 0 /* literal newline allowed in strings */
    else if (c=='\n')   putc('n');
#endif
    else if (c=='\t')   putc('t');
    else if (c=='\010') putc('b');
    else if (c=='\f')   putc('f');
    else if (c>=64 || p==pend || ULE(*p-'0','7'-'0')) {
      putc((c>>6&7)+'0');
      putc((c>>3&7)+'0');
      putc((   c&7)+'0');
    } else if (c>=8) {
      putc((c>>3)  +'0');
      putc((   c&7)+'0');
    } else putc(c+'0');
#endif
  }
  assert(nest==0);
  putc(')',curws.wf); curws.colc++;
}

/** @return the byte length of a string as a quoted PostScript hex string
 * literal
 */
static slen_t pstrhlen(register char const* p, char const* pend) {
  slen_t olen=2+2*(pend-p); /* '<' and '>' */
  if (p!=pend && (pend[-1]&15)==0) olen--;
  return olen;
}

/** Prints the specified string as a quoted PostScript hex string literal.
 * Does not modify curws.colc etc.
 */
static void pstrhput(register char const* p, char const* pend) {
  static char const hextable[]="0123456789abcdef";
  char c;
  curws.colc+=2+2*(pend-p);
  putc('<',curws.wf);
  if (p!=pend--) {
    while (p!=pend) {
      c=hextable[*(unsigned char const*)p>>4]; putc(c,curws.wf);
      c=hextable[*(unsigned char const*)p&15]; putc(c,curws.wf);
      p++;
    }
    c=hextable[*(unsigned char const*)p>>4]; putc(c,curws.wf);
    c=hextable[*(unsigned char const*)p&15]; if (c!='0') putc(c,curws.wf); else curws.colc--;
  }
  putc('>',curws.wf);
}

static void copy_token(char tok) {
  slen_t len, qlen, hlen;
  switch (tok) {
   case 0: erri("eof in copy", 0);
   case '[': case ']': case '<': case '>':
    len=ibufb-ibuf;
#if 0
    if (curws.colc+(len=ibufb-ibuf)>MAXLINE) newline();
#endif
    curws.lastclosed=TRUE;
   write:
#if 0
    if (len>MAXLINE) fprintf(stderr, "%s: warning: output line too long\n", PROGNAME);
#endif
    fwrite(ibuf, 1, len, curws.wf); curws.colc+=len;
    break;
   case '/':
    len=ibufb-ibuf;
#if 0
    if ((len=ibufb-ibuf)<IBUFSIZE && !(*ibufb='\0') && (c=findword(ibuf))>=0) {
      ibuf[0]='/';
      ibuf[1]=c;
      ibufb=ibuf+2;
      len=2;
    }
#endif
#if 0
    if (curws.colc+len>MAXLINE) newline();
#endif
    curws.lastclosed=FALSE;
    goto write;
#if 0 /* tags from ps_tiny.c */
   case '<': case '>':
#endif
   case '(':
    qlen=pstrqlen(ibuf,ibufb);
    hlen=pstrhlen(ibuf,ibufb);
#if 0
    if (curws.colc+qlen>MAXLINE) newline();
    if (qlen>MAXLINE) fprintf(stderr, "%s: warning: output string too long\n", PROGNAME);
#endif
    /* putc(ibuf[1]); */
    if (hlen<qlen) pstrhput(ibuf,ibufb); /* should never happen; hex is too long */
              else pstrqput(ibuf,ibufb);
    curws.lastclosed=TRUE;
    break;
   default: /* case '1': case '.': case 'E': case 'b': */
    /* Dat: ibuf_int is ignored for '1' */
    /* fprintf(stderr,"fw(%s) %c\n", ibuf, findword("32768")); */
    len=ibufb-ibuf;
#if 0
    if ((len=ibufb-ibuf)<IBUFSIZE && !(*ibufb='\0') && (c=findword_stripslash(ibuf))>=0) {
      ibuf[0]=c;
      ibufb=ibuf+1;
      len=1;
    }
#endif
#if 0
    if (curws.colc+len+!curws.lastclosed>MAXLINE) newline();
#else
    if (0) {}
#endif
    else if (curws.lastclosed) {}
    else if (curws.colc+len<MAXLINE) { putc(' ',curws.wf); curws.colc++; }
    else newline();
    curws.lastclosed=FALSE;
    goto write;
  }
}

/** Skips a whole recursive structure starting with `tok'. Works with `R' */
static void skipstruct(char tok, sbool copy_p) {
  slen_t nest=0, lastofs;
  pdfint_t b;
  while (1) {
    if (copy_p) copy_token(tok);
    switch (tok) {
     case 0: erri("eof in skipstruct",0);
     case '1': /* Skip a possible `R' */
      lastofs=currs.lastofs; /* Dat: copy_token() already called */
      if ('1'==(tok=gettok()) && (b=ibuf_int, TRUE) && 'R'==(tok=gettok())) {
        if (copy_p) {
          sprintf(ibuf, "%"SLEN_P"d", b); ibufb=ibuf+strlen(ibuf); copy_token('1');
          ibuf[0]='R'; ibuf[1]='\0'; copy_token('R');
        }
      } else r_seek(lastofs);
      break;
     case '[': case '<': /* Imp: treat dicts and arrays differently, create nest stack */
      nest++;
      break;
     case ']': case '>':
      if (nest--==0) erri("too many array/dict closes",0);
      break;
     default: ;
    }
    if (nest==0) break;
    tok=gettok();
  }
}

static pdfint_t gettok_int(char const* for_) {
  char tok;
  slen_t lastofs;
  pdfint_t a, b;
  if ('1'!=(tok=gettok())) erri("int expected for ", for_);
  a=ibuf_int; lastofs=currs.lastofs;
  if ('1'==(tok=gettok()) && (b=ibuf_int, TRUE) && 'R'==(tok=gettok())) {
    r_seek_obj(a,b); /* Imp: test this */
    if ('1'!=(tok=gettok())) erri("int expected (R) for ", for_);
    a=ibuf_int;
    r_seek(lastofs);
    ASSERT_SE('1'==, gettok());
    ASSERT_SE('R'==, gettok());
  } else {
    r_seek(lastofs);
  }
  return a;
}

static void r_seek_ref(void) {
  slen_t lastofs=ftell(currs.file);
  pdfint_t a, b;
  if ('1'==gettok() && (a=ibuf_int, TRUE)
   && '1'==gettok() && (b=ibuf_int, TRUE) && 'R'==gettok()
     ) {
    r_seek_obj(a,b);
  } else {
    r_seek(lastofs); /* seek back */
  }
}

/** @return file offset of previous xref table in file; or 0 */
static slen_t r_copy_trailer(void) {
  char tok;
  pdfint_t prev=0;
  if (gettok()!='E' || 0!=strcmp(ibuf,"trailer")) erri("trailer expected",0);
  if (gettok()!='<') erri("trailer dict expected",0);
  while (1) {
    if ('>'==(tok=gettok())) break;
    if ('/'!=tok) erri("trailer dict key expected",0);
    /* Dat: first trailer usually has: /Size /Info /Root /Prev /ID */
    /* Dat: prev trailers usually have: /Size /ID */
    /* Dat: thus our merged trailer will have many /ID fields */
    #if DEBUG
      fprintf(stderr,"trailer_key=(%s)\n",ibuf);
    #endif
    if (0==strcmp(ibuf,"/Prev")) {
      prev=gettok_int("trailer /Prev");
      if (prev<OBJ_MIN_OFS || prev+(slen_t)0>=currs.filesize) erri("invalid prev ofs",0);
    } else if (0==strcmp(ibuf,"/Size")) {
      skipstruct(gettok(), FALSE);
    } else {
      copy_token(tok);
      skipstruct(gettok(), FALSE); /* copy() */
    }
  }
  return prev;
}

/**
 * currs.file must be positioned just before `<<'. After this function,
 * currs.file will be positioned just after the dict key (i.e just before
 * the value). If the key isn't found, the file position is unchanged.
 * @param key dict key, e.g "/Root"
 * @return true iff found
 */
static sbool r_seek_dictval(char const* key) {
  char tok;
  pdfint_t prev=0;
  slen_t oldofs=ftell(currs.file);
  if (gettok()!='<') erri("dict expected",0);
  while (1) {
    if ('>'==(tok=gettok())) { r_seek(oldofs); return FALSE; }
    if ('/'!=tok) erri("dict key expected",0);
    /* ^^^ Dat: PDF keys must be names (PS allows others) */
    if (0==strcmp(ibuf,key)) return TRUE;
    skipstruct(gettok(), FALSE);
  }
  return prev;
}

static void r_seek_dictval_must(char const* key) {
  if (!r_seek_dictval(key)) erri("missing dict key", key);
}

/** @param typenam e.g "/Pages" */
static void r_checktype(char const* typenam) {
  slen_t oldofs=ftell(currs.file);
  if (!r_seek_dictval("/Type")) erri("missing /Type for dict", 0);
  r_seek_ref();
  if ('/'!=gettok() || 0!=strcmp(ibuf, typenam)) {
    if ((ibufb-ibuf)+strlen(typenam)+20>=IBUFSIZE) erri("expected type", typenam);
    sprintf(ibufb, ", needed %s", typenam);
    erri("dict type mismatch: got ", ibuf);
  }
  r_seek(oldofs);
}

static slen_t r_copy_trailer(void);

static void r_read_xref(void) {
  struct XrefEntry *e;
  char tok, xbuf[21];
  unsigned long dummy;
  pdfint_t xzero, xcount;
  slen_t prevofs;
  int n;
  currs.xreftc=1;
  currs.trailer1ofs=-1U;
  while (1) {
    if ((tok=gettok())!='E' || 0!=strcmp(ibuf,"xref")) { erri("expected xref",0); return; }
    if ((tok=gettok())!='1' || (xzero =ibuf_int)<0) { erri("expected xref base offset",0); return; }
    if ((tok=gettok())!='1' || (xcount=ibuf_int)<0) { erri("expected xref count",0); return; }
    #if DEBUG
      fprintf(stderr,"xref=(%lu+%lu)\n", xzero, xcount);
    #endif
    while ((n=getc(currs.file))>=0 && is_ps_white(n)) {}
    if (n>=0) ungetc(n,currs.file);
    if (xzero+xcount+(slen_t)0>currs.xrefc) {
      if (NULL==(currs.xrefs=realloc(currs.xrefs, sizeof(currs.xrefs[0])*(xzero+xcount)))) erri("out of memory for xref",0);
      memset(currs.xrefs+currs.xrefc, '\0', (xzero+xcount-currs.xrefc)*sizeof(currs.xrefs[0]));
      /* ^^^ Dat: initialize .type with '\0' */
      currs.xrefc=xzero+xcount;
    }
    e=currs.xrefs+xzero;
    xbuf[20]='\0';
    while (xcount--!=0) {
      if (20!=fread(xbuf, 1, 20, currs.file)
       || !is_digits(xbuf, xbuf+10)
       || !is_ps_white(xbuf[10])
       || !is_digits(xbuf+11, xbuf+16)
       || !is_ps_white(xbuf[16])
       || ((e->type=xbuf[17])!='n' && xbuf[17]!='f')
       || 2!=sscanf(xbuf, "%"SLEN_P"u%hu", &(e->ofs), &(e->gennum))
       || (dummy=e->gennum)>65535UL /* Dat: tmp= to pacify gcc-3.4 warning: comparison is always false due to limited range of data type */
       || (xbuf[17]=='n' && (e->ofs<OBJ_MIN_OFS || e->ofs>=currs.filesize))
         ) erri("invalid xref entry",0);
      e++;
    }
    if (currs.trailer1ofs==-1U) currs.trailer1ofs=ftell(currs.file);
    if (0==(prevofs=r_copy_trailer())) break;
    r_seek(prevofs);
    currs.xreftc++;
  }
  /* Now find currs.catalogofs */
  r_seek(currs.trailer1ofs);
  ASSERT_SE('E'==,gettok()); /* skip `trailer' */
  r_seek_dictval_must("/Root"); r_seek_ref();
  currs.catalogofs=ftell(currs.file);

  #if DEBUG
    printf("Input PDF (%s): filesize=%"SLEN_P"u, xrefc=%"SLEN_P"u, xreftc=%u, catalogofs=%"SLEN_P"d\n",
      currs.filename, currs.filesize, currs.xrefc, currs.xreftc, currs.catalogofs);
  #endif
  r_seek(currs.catalogofs);
  r_checktype("/Catalog");
  r_seek_dictval_must("/Pages"); r_seek_ref();
  #if DEBUG
    fprintf(stderr, "/Pages at=%ld\n", ftell(currs.file));
  #endif
  currs.uppagesofs=ftell(currs.file);
  r_checktype("/Pages");
  r_seek_dictval_must("/Count");
  if (0>(xcount=gettok_int("pagecount"))) erri("page count <0",ibuf);
  curws.pagetotal+=currs.pagecount=xcount;
}

static struct XrefEntry *enq_first=NULL, **enq_lastp=&enq_first;

#define ENQ_PUT(xe) (*enq_lastp=(xe), (xe)->next=NULL, enq_lastp=&((xe)->next))
#define ENQ_RESET() (enq_first=NULL, enq_lastp=&enq_first)

/** Skips a whole recursive structure starting with `tok'. Works with `R' */
static void wr_enqueue_struct(sbool copy_p) {
  struct XrefEntry *e;
  char tok;
  slen_t nest=0, lastofs;
  pdfint_t a, b;
  /* enqueue_stream_length=-1; */
  while (1) {
    if (0==(tok=gettok())) erri("eof in e_s", 0);
    #if DEBUG
      ibufb[0]='\n'; ibufb[1]='\0'; fputs(ibuf,stderr);
    #endif
    if (copy_p && tok!='1') copy_token(tok);
    switch (tok) {
     case '1': /* Skip a possible `R' */
      a=ibuf_int;
      lastofs=currs.lastofs; /* Dat: copy_token() already called */
      if ('1'==(tok=gettok()) && (b=ibuf_int, TRUE) && 'R'==(tok=gettok())) {
        e=objentry(a,b);
        #if DEBUG
          fprintf(stderr,"XUT %ld (%ld %ld obj)\n", e->target_num, a, b);
        #endif
        if (e->target_num==0) {
          e->target_num=curws.outobjc++;
          #if DEBUG
            fprintf(stderr, "PUT\n");
          #endif
          ENQ_PUT(e);
        }
        if (copy_p) {
          sprintf(ibuf, "%"SLEN_P"d 0 R", e->target_num); ibufb=ibuf+strlen(ibuf);
          copy_token('1');
        }
      } else {
        if (copy_p) {
          sprintf(ibuf, "%"SLEN_P"d", a); ibufb=ibuf+strlen(ibuf);
          copy_token('1');
        }
        r_seek(lastofs);
      }
      break;
     case '[': case '<': /* Imp: treat dicts and arrays differently, create nest stack */
      nest++;
      break;
     case ']': case '>':
      if (nest--==0) erri("too many array/dict closes in e_s",0);
      break;
     default: ;
    }
    if (nest==0) break;
  }
}

static void w_dump_start(void) {
  if (0!=fseek(curws.wf, 0, SEEK_SET)) errn("cannot begin dump",curws.filename);
  fprintf(curws.wf, "%s%s", currs.pdf_header, currs.is_binary ? "%\xE1\xE9\xF3\xFA\n" : "");
  curws.is_binary=currs.is_binary; /* Imp: pre-look other inputs */
  curws.outobjc=2;
  curws.txrefa=0;
  curws.txrefc=0;
  curws.txrefs=NULL;
  curws.lastclosed=TRUE;
}

static void w_xref_aset(slen_t num, slen_t ofs) {
  if (num>=curws.txrefa) {
    #ifdef __CHECKER__Z
      slen_t oa=curws.txrefa;
    #endif
    if (curws.txrefa<16) curws.txrefa=16;
    while (curws.txrefa<=num) curws.txrefa<<=1;
    if (NULL==(curws.txrefs=realloc(curws.txrefs, curws.txrefa*sizeof(curws.txrefs[0])))) errn("out of memory for xref_aset",0);
    #ifdef __CHECKER__Z
      memset(curws.txrefs+oa, '\0'
    #endif
  }
  if (num>=curws.txrefc) {
    memset(curws.txrefs+curws.txrefc, '\0', sizeof(curws.txrefs[0])*(num-curws.txrefc));
    curws.txrefc=num+1;
  }
  curws.txrefs[num]=ofs;
}

static void w_dump_xref(void) {
  slen_t const *p=curws.txrefs, *pend=p+curws.txrefc;
  if (!curws.lastclosed) putc('\n',curws.wf);
  curws.startxrefofs=ftell(curws.wf);
  fprintf(curws.wf, "xref\n0 %"SLEN_P"u\n", curws.txrefc); /* Dat: must be "\n" */
  while (p!=pend) {
    if (*p!=0) {
      if (*p/1000000U>=10000U) errn("offset overflow",0); /* Dat: works with 32 bit arithmetic */
      fprintf(curws.wf, "%010"SLEN_P"u 00000 n \n", *p++);
    } else { fprintf(curws.wf, "0000000000 65535 f \n"); p++; }
  }
  curws.lastclosed=TRUE; curws.colc=0;
}

static void wr_enqueue_catalog(void) {
  char tok;
  if (gettok()!='<') erri("catalog dict expected",0);
  copy_token('<');
  while (1) {
    copy_token(tok=gettok());
    if ('>'==tok) break;
    if ('/'!=tok) erri("catalog dict key expected",0);
    if (0==strcmp(ibuf,"/Pages")) { /* must be an indirect reference */
      struct XrefEntry *e;
      slen_t lastofs=ftell(currs.file);
      pdfint_t a, b;
      if ('1'==gettok() && (a=ibuf_int, TRUE)
       && '1'==gettok() && (b=ibuf_int, TRUE) && 'R'==gettok()
         ) {} else { r_seek(lastofs); erri("/Pages of /Catalog must be indirect", 0); }
      r_seek(lastofs);
      e=objentry(a,b);
      wr_enqueue_struct(FALSE);
      curws.lastsrcpages_num=e->target_num;
      sprintf(ibuf, "1 0 R"); ibufb=ibuf+strlen(ibuf);
      copy_token('1');
    } else {
      wr_enqueue_struct(TRUE);
    }
  }
}

static void wr_enqueue_uppages(void) {
  char tok;
  if (gettok()!='<') erri("uppages dict expected",0);
  copy_token('<');
  sprintf(ibuf,"/Parent"); ibufb=ibuf+strlen(ibuf); copy_token('/');
  sprintf(ibuf,"1 0 R");   ibufb=ibuf+strlen(ibuf); copy_token('1');
  while (1) {
    copy_token(tok=gettok());
    if ('>'==tok) break;
    if ('/'!=tok) erri("uppages dict key expected",0);
    if (0==strcmp(ibuf,"/Parent")) { /* must be an indirect reference */
      /* Dat: top /Pages doesn't have /Parent, but ensure */
      skipstruct(gettok(), FALSE);
    } else {
      wr_enqueue_struct(TRUE);
    }
  }
}

/** Reads all objs reachable from currs, and dumps them to curws in order */
static void r_dump_reachable(void) {
  struct XrefEntry *e;
  pdfint_t streamlen;
  slen_t lastofs;
  char tok;
  ENQ_RESET();
  r_seek(currs.trailer1ofs);
  skipstruct(gettok(), FALSE); /* `trailer' */
  wr_enqueue_struct(FALSE);
  while (enq_first!=NULL) {
    e=enq_first;
    #if DEBUG
      fprintf(stderr,"dumping_src=(%u)\n", e-currs.xrefs);
    #endif
    if (!curws.lastclosed) putc('\n',curws.wf);
    w_xref_aset(e->target_num, ftell(curws.wf));
    #if 0
      fprintf(stderr, "%"SLEN_P"u 0 obj # from %lu\n", e->target_num, e->ofs);
    #endif
    fprintf(curws.wf, "%"SLEN_P"u 0 obj\n", e->target_num);
    curws.lastclosed=TRUE; curws.colc=0;
    r_seek(e->ofs);
    if ('1'!=gettok() || '1'!=gettok()
     || 'E'!=gettok() || 0!=strcmp(ibuf,"obj")
       ) erri("obj start expected",0);
    lastofs=ftell(currs.file);
    #if DEBUG
      fprintf(stderr, "CMP %ld %ld\n", lastofs, currs.catalogofs);
    #endif
         if (lastofs==currs.catalogofs) wr_enqueue_catalog();
    else if (lastofs==currs.uppagesofs) wr_enqueue_uppages();
                                   else wr_enqueue_struct(TRUE);
    if ('E'!=(tok=gettok())) erri("name expected after obj",0);
    if (0==strcmp(ibuf,"stream")) {
      int i;
      slen_t afterofs=ftell(currs.file);
      r_seek(lastofs);
      r_seek_dictval_must("/Length"); /* BUGFIX at Sun Mar  7 18:37:23 CET 2004 */
      streamlen=gettok_int("dump");
      if (streamlen<0) erri("negative stream length",0);
      r_seek(afterofs);
      if (!curws.lastclosed) putc('\n',curws.wf);
      fprintf(curws.wf, "stream\n"); /* no "\r", to avoid confusion */
      while (1) { /* Imp: why this while(1)? */
        /* Dat: PDFRef.pdf subsection 3.2.7 says that "\r\n" mustn't follow `stream' -- but in the file PDFRef.pdf, it does */
        if ((i=getc(currs.file))=='\r') {
          i=getc(currs.file);
          if (i!='\n' && i!=-1) r_seek(ftell(currs.file)-1);
          break;
        } else if (is_ps_white(i)) { break; }
        else { r_seek(ftell(currs.file)-1); break; }
      }
      while (streamlen!=0) {
        if (0==(afterofs=fread(ibuf, 1, streamlen>IBUFSIZE ? IBUFSIZE : streamlen, currs.file))) erri("stream too short",0);
        fwrite(ibuf, 1, afterofs, curws.wf);
        streamlen-=afterofs;
      }
      curws.lastclosed=TRUE; curws.colc=0;
      if ('E'!=gettok() || 0!=strcmp(ibuf,"endstream")) erri("endstream expected",0);
      copy_token('E');
      tok=gettok();
    }
    if ('E'!=tok || 0!=strcmp(ibuf,"endobj")) erri("endobj expected",0);
    copy_token('E');
    enq_first=e->next; /* this must be done as late as possible (afte ENQ_PUT()s) */
  }
}

static void w_make_trailer(void) {
  char tok;
  slen_t pretofs;
  r_seek(currs.trailer1ofs);
  if (gettok()!='E' || 0!=strcmp(ibuf,"trailer")) erri("trailer expected for dump",0);
  newline();
  pretofs=ftell(curws.wf);
  copy_token('E'); newline();
  if (gettok()!='<') erri("trailer dict expected",0);
  copy_token('<');
  while (1) {
    if ('>'==(tok=gettok())) break;
    if ('/'!=tok) erri("trailer dict key expected",0);
    /* Dat: first trailer usually has: /Size /Info /Root /Prev /ID */
    /* Dat: prev trailers usually have: /Size /ID -- will be ignored */
    /* Dat: thus our merged trailer will have many /ID fields -- but we don't merge prev trailers */
    #if DEBUG
      fprintf(stderr,"trailer_key=(%s)\n",ibuf);
    #endif
    if (0==strcmp(ibuf,"/Prev") || 0==strcmp(ibuf,"/Size")) {
      skipstruct(gettok(), FALSE);
    } else {
      copy_token(tok);
      wr_enqueue_struct(TRUE); /* renumbering */
    }
  }
  if (0!=fseek(curws.wf, pretofs, SEEK_SET)) errn("cannot seek pretofs: ",curws.filename);
  curws.lastclosed=TRUE; curws.colc=0;
}
  
static void w_dump_trailer(void) {
  newline();
  fwrite(curws.trailer, 1, curws.trailerlen, curws.wf);
  fprintf(curws.wf,"/Size %"SLEN_P"u>>\nstartxref\n%"SLEN_P"u\n%%%%EOF\n", curws.txrefc, curws.startxrefofs); /* Dat: must end by "%%EOF\n" */
  fflush(curws.wf);
  curws.lastclosed=TRUE; curws.colc=0;
}

/** curws now contains a trailer dict. Read it to memory. ftruncate() isn't
 * necessary, because final output will be longer than the trailer */
static void w_pull_trailer(void) {
  slen_t ofs=ftell(curws.wf);
  if (0!=fseek(curws.wf, 0, SEEK_END)) { seekerr: errn("cannot seek: ", curws.filename); }
  if (NULL==(curws.trailer=malloc(1+(curws.trailerlen=ftell(curws.wf)-ofs)))) errn("out of memory for trailer",0);
  if (0!=fseek(curws.wf, ofs, SEEK_SET)) goto seekerr;
  if (curws.trailerlen!=fread(curws.trailer, 1, curws.trailerlen, curws.wf)) errn("cannot read trailer: ", curws.filename);
  if (0!=fseek(curws.wf, ofs, SEEK_SET)) goto seekerr; /* superfluous, but ANSI needs it */
}

static void w_dump_toppages(void) {
  /* Dat: we must say `1 0 obj' for (data flow to) /Parent of /Pages */
  slen_t srci;
  newline();
  w_xref_aset(1, ftell(curws.wf));
  sprintf(ibuf, "1 0 obj\n<</Type/Pages/Count %"SLEN_P"u/Kids[", curws.pagetotal);
  ibufb=ibuf+strlen(ibuf); copy_token('[');
  srci=0; while (srci!=curws.srcpages_numc) {
    sprintf(ibuf, "%"SLEN_P"u", curws.srcpages_nums[srci++]);
    ibufb=ibuf+strlen(ibuf); copy_token('1');
    ibuf[0]='0'; ibuf[1]='\0'; ibufb=ibuf+1; copy_token('1');
    ibuf[0]='R'; ibuf[1]='\0'; ibufb=ibuf+1; copy_token('R');
  }
  sprintf(ibuf, "]>>"); ibufb=ibuf+strlen(ibuf); copy_token(']');
  sprintf(ibuf, "endobj"); ibufb=ibuf+strlen(ibuf); copy_token('E');
}

static void r_open(char const *filename) {
  currs.xrefs=NULL; currs.xrefc=0; currs.lastofs=0;
  currs.filename=filename;
  if (!(currs.file=fopen(currs.filename,"rb"))) {
    fprintf(stderr, "%s: open %s: %s\n", PROGNAME, currs.filename, strerror(errno));
    exit(3);
  }
  if (0!=fseek(currs.file, 0, SEEK_END)) {
    fprintf(stderr, "%s: unseekable %s: %s\n", PROGNAME, currs.filename, strerror(errno));
    exit(6);
  }
  { long l=ftell(currs.file); currs.filesize=l;
    if (l<32 || currs.filesize!=l+0UL) {
      fprintf(stderr, "%s: invalid filesize for %s: %ld\n", PROGNAME, currs.filename, l);
      exit(7);
    }
  }
}

static void r_input_status(void) {
  printf("Input PDF (%s): filesize=%"SLEN_P"u, xrefc=%"SLEN_P"u, xreftc=%"SLEN_P"u, catalogofs=%"SLEN_P"u, #pages=%"SLEN_P"u, is_binary=%d\n",
    currs.filename, currs.filesize, currs.xrefc, currs.xreftc, currs.catalogofs, currs.pagecount, currs.is_binary);
}
static void r_close(void) {  
  free(currs.xrefs); currs.xrefs=NULL;
  if (ferror(currs.file)) erri("error reading file: ", currs.filename);
  fclose(currs.file); currs.file=NULL;
  currs.filename=NULL;
}

static void w_output_status(void) {
  printf("Output PDF (%s): filesize=%lu, xrefc=%"SLEN_P"u, subfiles=%"SLEN_P"u, #pages=%"SLEN_P"u, is_binary=%d\n",
    curws.filename, (long)ftell(curws.wf), curws.txrefc, curws.srcpages_numc, curws.pagetotal, curws.is_binary);
}

/* --- Main */

int main(int argc, char const* const*argv) {
  char const*const* ap;
  slen_t srci;
  (void)argc; (void)argv;
  if (argc<3 || 0!=strcmp(argv[1],"-o")) {
    fprintf(stderr, "Usage: %s -o <output.pdf> <input1.pdf> [...]\n", argv[0]);
    exit(2);
  }

  curws.colc=0; curws.lastclosed=TRUE; curws.pagetotal=0;
  curws.filename=argv[2];
  { ap=argv+3;
    while (*ap) if (0==strcmp(curws.filename, *ap++)) {
      fprintf(stderr, "%s: may not append to existing PDF: %s\n", PROGNAME, curws.filename);
      exit(4);
    }
    curws.srcpages_numc=ap-argv-3;
    /* fprintf(stderr,"%d\n", curws.srcpages_numc); */
  }
  if (!(curws.wf=fopen(curws.filename,"wb+"))) {
    fprintf(stderr, "%s: open4write %s: %s\n", PROGNAME, curws.filename, strerror(errno));
    exit(5);
  }
  if (NULL==(curws.srcpages_nums=malloc(sizeof(curws.srcpages_nums[0])*curws.srcpages_numc))) errn("out of memory for srcpages_nums",0);
  
  r_open(argv[3]);
  r_check_pdf_header();
  r_seek_xref();
  r_read_xref();
  r_input_status();
  w_dump_start();
  r_dump_reachable();
  w_make_trailer();
  w_pull_trailer();
  r_close();
  curws.srcpages_nums[0]=curws.lastsrcpages_num;

  ap=argv+4; srci=1;
  while (*ap) {
    r_open(*ap++);
    r_check_pdf_header();
    r_seek_xref();
    r_read_xref();
    r_input_status();
    r_dump_reachable();
    r_close();
    curws.srcpages_nums[srci++]=curws.lastsrcpages_num;
  }

  w_dump_toppages();
  w_dump_xref();
  w_dump_trailer();
  fflush(curws.wf);
  w_output_status();
  if (ferror(curws.wf)) errn("error writing output file: ", curws.filename);
  free(curws.trailer);
  free(curws.srcpages_nums);
  if (curws.txrefs!=NULL) free(curws.txrefs);
  return 0;
}

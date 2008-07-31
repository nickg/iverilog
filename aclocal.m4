# AX_CPP_IDENT
# ------------
# Check if the C compiler supports #ident
# Define and substitute ident_support if so.
#
# It would be simpler and more consistent with the rest of the autoconf
# structure to AC_DEFINE(HAVE_CPP_IDENT) instead of
# ident_support='-DHAVE_CVS_IDENT=1' and AC_SUBST(ident_support), but that
# change would require all C files in the icarus top level directory to
# put #include <config.h> before the #ifdef HAVE_CVS_IDENT (and change
# HAVE_CVS_IDENT to HAVE_CPP_IDENT).   That would also remove all special
# ident_support handling from the Makefile.  Manyana.
#
AC_DEFUN([AX_CPP_IDENT],
[AC_CACHE_CHECK([for ident support in C compiler], ax_cv_cpp_ident,
[AC_TRY_COMPILE([
#ident "$Id: aclocal.m4,v 1.7 2007/05/16 23:59:12 steve Exp $"
],[while (0) {}],
[AS_VAR_SET(ax_cv_cpp_ident, yes)],
[AS_VAR_SET(ax_cv_cpp_ident, no)])])
if test $ax_cv_cpp_ident = yes; then
  ident_support='-DHAVE_CVS_IDENT=1'
fi
AC_SUBST(ident_support)
])# AC_CPP_IDENT


# _AX_C_UNDERSCORES_MATCH_IFELSE(PATTERN, ACTION-IF-MATCH, ACTION-IF-NOMATCH)
# ------------------------------
# Sub-macro for AX_C_UNDERSCORES_LEADING and AX_C_UNDERSCORES_TRAILING.
# Unwarranted assumptions:
#   - the object file produced by AC_COMPILE_IFELSE is called "conftest.$ac_objext"
#   - the nm(1) utility is available, and its name is "nm".
AC_DEFUN([_AX_C_UNDERSCORES_MATCH_IF],
[AC_COMPILE_IFELSE([void underscore(void){}],
[AS_IF([nm conftest.$ac_objext|grep $1 >/dev/null 2>/dev/null],[$2],[$3])],
[AC_MSG_ERROR([underscore test crashed])]
)])


# AX_C_UNDERSCORES_LEADING
# ---------------------------------
# Check if symbol names in object files produced by C compiler have
# leading underscores. Define NEED_LU if so.
AC_DEFUN([AX_C_UNDERSCORES_LEADING],
[AC_CACHE_CHECK([for leading underscores], ax_cv_c_underscores_leading,
[_AX_C_UNDERSCORES_MATCH_IF([_underscore],
[AS_VAR_SET(ax_cv_c_underscores_leading, yes)],
[AS_VAR_SET(ax_cv_c_underscores_leading, no)])])
if test $ax_cv_c_underscores_leading = yes -a "$CYGWIN" != "yes" -a "$MINGW32" != "yes"; then
 AC_DEFINE([NEED_LU], [1], [Symbol names in object files produced by C compiler have leading underscores.])
fi
])# AX_C_UNDERSCORES_LEADING


# AX_C_UNDERSCORES_TRAILING
# ---------------------------------
# Check if symbol names in object files produced by C compiler have
# trailing underscores.  Define NEED_TU if so.
AC_DEFUN([AX_C_UNDERSCORES_TRAILING],
[AC_CACHE_CHECK([for trailing underscores], ax_cv_c_underscores_trailing,
[_AX_C_UNDERSCORES_MATCH_IF([underscore_],
[AS_VAR_SET(ax_cv_c_underscores_trailing, yes)],
[AS_VAR_SET(ax_cv_c_underscores_trailing, no)])])
if test $ax_cv_c_underscores_trailing = yes; then
 AC_DEFINE([NEED_TU], [1], [Symbol names in object files produced by C compiler have trailing underscores.])
fi
])# AX_C_UNDERSCORES_TRAILING

# AX_WIN32
# --------
# Combined check for several flavors of Microsoft Windows so
# their "issues" can be dealt with
AC_DEFUN([AX_WIN32],
[AC_CYGWIN
AC_MINGW32
WIN32=no
AC_MSG_CHECKING([for Microsoft Windows])
if test "$CYGWIN" = "yes" -o "$MINGW32" = "yes"
then
    WIN32=yes
fi
AC_SUBST(MINGW32)
AC_SUBST(WIN32)
AC_MSG_RESULT($WIN32)
])# AX_WIN32

# AX_LD_EXTRALIBS
# ---------------
# mingw needs to link with libiberty.a, but cygwin alone can't tolerate it
AC_DEFUN([AX_LD_EXTRALIBS],
[AC_MSG_CHECKING([for extra libs needed])
EXTRALIBS=
case "${host}" in
     *-*-cygwin* )
        if test "$MINGW32" = "yes"; then
            EXTRALIBS="-liberty"
        fi
        ;;
esac
AC_SUBST(EXTRALIBS)
AC_MSG_RESULT($EXTRALIBS)
])# AX_LD_EXTRALIBS

# AX_LD_SHAREDLIB_OPTS
# --------------------
# linker options when building a shared library
AC_DEFUN([AX_LD_SHAREDLIB_OPTS],
[AC_MSG_CHECKING([for shared library link flag])
shared=-shared
case "${host}" in
     *-*-cygwin*)
        shared="-shared -Wl,--enable-auto-image-base"
        ;;

     *-*-hpux*)
        shared="-b"
        ;;

     *-*-darwin1.[0123])
        shared="-bundle -undefined suppress"
        ;;

     *-*-darwin*)
        shared="-bundle -undefined suppress -flat_namespace"
        ;;
esac
AC_SUBST(shared)
AC_MSG_RESULT($shared)
])# AX_LD_SHAREDLIB_OPTS

# AX_C_PICFLAG
# ------------
# The -fPIC flag is used to tell the compiler to make position
# independent code. It is needed when making shared objects.
AC_DEFUN([AX_C_PICFLAG],
[AC_MSG_CHECKING([for flag to make position independent code])
PICFLAG=-fPIC
case "${host}" in

     *-*-cygwin*)
        PICFLAG=
        ;;

     *-*-hpux*)
        PICFLAG=+z
        ;;

esac
AC_SUBST(PICFLAG)
AC_MSG_RESULT($PICFLAG)
])# AX_C_PICFLAG

# AX_LD_RDYNAMIC
# --------------
# The -rdynamic flag is used by iverilog when compiling the target,
# to know how to export symbols of the main program to loadable modules
# that are brought in by -ldl
AC_DEFUN([AX_LD_RDYNAMIC],
[AC_MSG_CHECKING([for -rdynamic compiler flag])
rdynamic=-rdynamic
case "${host}" in

    *-*-netbsd*)
        rdynamic="-Wl,--export-dynamic"
        ;;

    *-*-openbsd*)
        rdynamic="-Wl,--export-dynamic"
        ;;

    *-*-solaris*)
        rdynamic=""
        ;;

    *-*-cygwin*)
        rdynamic=""
        ;;

    *-*-hpux*)
        rdynamic="-E"
        ;;

    *-*-darwin*)
        rdynamic="-Wl,-all_load"
        strip_dynamic="-SX"
        ;;

esac
AC_SUBST(rdynamic)
AC_MSG_RESULT($rdynamic)
AC_SUBST(strip_dynamic)
# since we didn't tell them we're "checking", no good place to tell the answer
# AC_MSG_RESULT($strip_dynamic)
])# AX_LD_RDYNAMIC

# AX_CPP_PRECOMP
# --------------
AC_DEFUN([AX_CPP_PRECOMP],
[# Darwin requires -no-cpp-precomp
case "${host}" in
    *-*-darwin*)
        CPPFLAGS="-no-cpp-precomp $CPPFLAGS"
        CFLAGS="-no-cpp-precomp $CFLAGS"
        ;;
esac
])# AX_CPP_PRECOMP

# Macro to support --enable-coverage option to build
# iverilog with code coverage statistics.
AC_DEFUN([AX_CODE_COVERAGE],
[
  AC_ARG_ENABLE(coverage,
    [AS_HELP_STRING([--enable-coverage], [Enable code coverage statistics.])],
    [if test "$enableval" = "yes" ; then
       # Explicitly turn off optimisation
       # This flag goes at the end so as to override any
       # other -Ox flags
       CXXFLAGS="-fprofile-arcs -ftest-coverage $CXXFLAGS -O0"
       CFLAGS="-fprofile-arcs -ftest-coverage $CFLAGS -O0"
       LDFLAGS="-fprofile-arcs $LDFLAGS"
       TGTLDFLAGS="-fprofile-arcs $TGTLDFLAGS"
     fi])
  AC_SUBST(TGTLDFLAGS)
])# AX_CODE_COVERAGE

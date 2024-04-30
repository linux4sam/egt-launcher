AC_PREREQ([2.69])
AC_INIT([egt-launcher],[1.4])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_MACRO_DIR([m4])
AC_USE_SYSTEM_EXTENSIONS
AC_CANONICAL_TARGET
PKG_PROG_PKG_CONFIG

AM_INIT_AUTOMAKE([1.10 foreign subdir-objects])

# Enable quiet compiles on automake 1.11.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_LANG(C++)
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX([17], [noext])
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_UINT32_T

AX_PTHREAD([LIBS="$PTHREAD_LIBS $LIBS"
            CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
            CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"],
            AC_MSG_ERROR(Can not find pthreads.  This is required.))

AC_DEFUN([EGT_CC_TRY_FLAG], [
  AC_MSG_CHECKING([whether $CC supports $1])

  egt_save_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $1 -Werror"

  AC_COMPILE_IFELSE([AC_LANG_SOURCE([ ])], [egt_cc_flag=yes], [egt_cc_flag=no])
  CFLAGS="$egt_save_CFLAGS"

  if test "x$egt_cc_flag" = "xyes"; then
    ifelse([$2], , :, [$2])
  else
    ifelse([$3], , :, [$3])
  fi
  AC_MSG_RESULT([$egt_cc_flag])
])

MAYBE_WARN="-Wall -Wextra \
-pedantic \
-Wfloat-equal \
-Wsign-compare \
-Wpointer-arith \
-Wwrite-strings \
-Wmissing-declarations \
-Wpacked \
-Wstrict-aliasing=2 -Winit-self \
-Wno-attributes -Wno-long-long \
-Wno-missing-field-initializers \
-Wno-unused-parameter \
-Wno-psabi" # no warnings about gcc abi changes

# invalidate cached value if MAYBE_WARN has changed
if test "x$egt_cv_warn_maybe" != "x$MAYBE_WARN"; then
	unset egt_cv_warn_cflags
fi
AC_CACHE_CHECK([for supported warning flags], egt_cv_warn_cflags, [
	echo
	WARN_CFLAGS=""

	# Some warning options are not supported by all versions of
	# gcc, so test all desired options against the current
	# compiler.
	#
	# Note that there are some order dependencies
	# here. Specifically, an option that disables a warning will
	# have no net effect if a later option then enables that
	# warnings, (perhaps implicitly). So we put some grouped
	# options (-Wall and -Wextra) up front and the -Wno options
	# last.

	for W in $MAYBE_WARN; do
		EGT_CC_TRY_FLAG([$W], [WARN_CFLAGS="$WARN_CFLAGS $W"])
	done

	egt_cv_warn_cflags=$WARN_CFLAGS
	egt_cv_warn_maybe=$MAYBE_WARN

	AC_MSG_CHECKING([which warning flags were supported])])
WARN_CFLAGS="$egt_cv_warn_cflags"
AC_SUBST(WARN_CFLAGS)

PKG_CHECK_MODULES(LIBEGT, [libegt >= 1.9], [], [
   AC_MSG_ERROR(libegt not found.  This is required.)
])

AC_ARG_ENABLE([lto],
  [AS_HELP_STRING([--enable-lto], [enable gcc's LTO [default=no]])],
  [enable_lto=$enableval], [enable_lto=no])
if test "x$enable_lto" = "xyes" ; then
  AC_MSG_NOTICE([enabling LTO])
  CFLAGS="$CFLAGS -flto"
  CXXFLAGS="$CXXFLAGS -flto"
fi

save_CXXFLAGS="${CXXFLAGS}"
save_CPPFLAGS="${CPPFLAGS}"
save_LDFLAGS="${LDFLAGS}"
CXXFLAGS="${LIBEGT_CFLAGS} ${CXXFLAGS}"
CPPFLAGS="${LIBEGT_CFLAGS} ${CPPFLAGS}"
LDFLAGS="${LIBEGT_LIBS} ${LDFLAGS}"

AC_CHECK_HEADERS([egt/detail/screen/kmsscreen.h])

CXXFLAGS="${save_CXXFLAGS}"
CPPFLAGS="${save_CPPFLAGS}"
LDFLAGS="${save_LDFLAGS}"

AC_PATH_PROGS([CLANG_TIDY_BIN], [clang-tidy-9 clang-tidy-6.0 clang-tidy])
AM_CONDITIONAL([HAVE_CLANG_TIDY_BIN], [test -n "$CLANG_TIDY_BIN"])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

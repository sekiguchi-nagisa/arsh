/*
 * Copyright (C) 2024 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MISC_LIB_PTY_HPP
#define MISC_LIB_PTY_HPP

#include <termios.h>

#if !defined(__CYGWIN__)
#include <sys/ttydefaults.h>
#endif

BEGIN_MISC_LIB_NAMESPACE_DECL

inline void xcfmakesane(termios &term) {
  term.c_iflag = TTYDEF_IFLAG;
  term.c_oflag = TTYDEF_OFLAG;
  term.c_lflag = TTYDEF_LFLAG;
  term.c_cflag = TTYDEF_CFLAG;
  cfsetispeed(&term, TTYDEF_SPEED);
  cfsetospeed(&term, TTYDEF_SPEED);

  // set to default control characters
  cc_t defchars[NCCS] = {0};
#ifdef VDISCARD
  defchars[VDISCARD] = CDISCARD;
#endif

#ifdef VDSUSP
  defchars[VDSUSP] = CDSUSP;
#endif

#ifdef VEOF
  defchars[VEOF] = CEOF;
#endif

#ifdef VEOL
  defchars[VEOL] = CEOL;
#endif

#ifdef VEOL2
  defchars[VEOL2] = CEOL;
#endif

#ifdef VERASE
  defchars[VERASE] = CERASE;
#endif

#ifdef VINTR
  defchars[VINTR] = CINTR;
#endif

#ifdef VKILL
  defchars[VKILL] = CKILL;
#endif

#ifdef VLNEXT
  defchars[VLNEXT] = CLNEXT;
#endif

#ifdef VMIN
  defchars[VMIN] = CMIN;
#endif

#ifdef VQUIT
  defchars[VQUIT] = CQUIT;
#endif

#ifdef VREPRINT
  defchars[VREPRINT] = CREPRINT;
#endif

#ifdef VSTART
  defchars[VSTART] = CSTART;
#endif

#ifdef VSTATUS
  defchars[VSTATUS] = CSTATUS;
#endif

#ifdef VSTOP
  defchars[VSTOP] = CSTOP;
#endif

#ifdef VSUSP
  defchars[VSUSP] = CSUSP;
#endif

#ifdef VSWTCH
  defchars[VSWTCH] = CSWTCH;
#endif

#ifdef VTIME
  defchars[VTIME] = CTIME;
#endif

#ifdef VWERASE
  defchars[VWERASE] = CWERASE;
#endif

  memcpy(term.c_cc, defchars, std::size(defchars) * sizeof(cc_t));
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_PTY_HPP

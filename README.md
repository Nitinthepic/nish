# Objective
  Try and create a shell which, eventually (and hopefully), reaches feature
  parity with bash

# Features
  * Launches processes
  * Supports Job Control and Signal Handling
  * Jobs/Pipes are given their own PGID and handled according to GNU C library
  * Certain builtins support piping
  * Readline for bash style use of arrow keys and history
  * Persistent history saved to file

# Things to add
  * Aliases
  * Redirects '<'
  * Get rid of memory leaks
  * Refactor code so it isn't in a single file

# Disclamers
  This is not a ready for release shell. It should not be used outside of the
  context of tinkering and hobbyism. This is a just a project of mine to see how
  much of a shell I can recreate without looking at the source code of other
  shells too much. If you are working on a school project, please don't use my
  code, that could be academic dishonesty depending on your school's policy. I
  would suggest, if your professor allows it, to look at CH. 28 of the GNU C
  Library manual.

# Online Resources I Used
  I spent a lot of time figuring out how POSIX job controlled worked with the
  GNU C Library manual; the sections 24 and 28 in particular were of major help.

  To figure out how pipes worked and how the system call worked, I relied on two
  main resources the geeks4geeks page and the man page, listed below
  respectively.

  https://www.geeksforgeeks.org/pipe-system-call/
  https://www.man7.org/linux/man-pages/man2/pipe.2.html

  To figure out how to use waitpid, I relied on its man page to help with the
  MACROS, the FLAGS, and the how the return values change based on said flags.
  From my understanding, waitpid is used primarily for terminated/stopped/exited
  processes for the parent to reap the child and to get information on how the
  child died. Using waitpid with WNOHANG, means it returns 0 if it cannot get
  status of a child, which we can use to see if a child is still running. With
  WUNTRACED, it means that waitpid will return if the child has stopped.

  I, when in doubt, repeatedly used bash and ps as sanity checks to see what
  expected behavior was to be.

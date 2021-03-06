/* ================================================================ *
   Mint
 * ================================================================ */

#include "mint/support/Assert.h"
#include "mint/support/CommandLine.h"
#include "mint/support/Diagnostics.h"
#include "mint/support/OSError.h"
#include "mint/support/OStream.h"
#include "mint/support/Process.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_ERRNO_H
#include <errno.h>
#endif

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

#if HAVE_STRING_H
#include <string.h>
#endif

#if HAVE_POLL_H
#include <poll.h>
#endif

#if HAVE_STDIO_H
#include <stdio.h>
#endif

#if HAVE_STDINT_H
#include <stdint.h>
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#if defined(_WIN32)
  #include <windows.h>
  #undef min
  #define R_OK 04
  #define W_OK 02
  typedef SSIZE_T ssize_t;
#endif

//extern char **environ;

namespace mint {

cl::Option<bool> optVerbose("verbose", cl::Group("global"),
    cl::Description("Print each command run."));

// -------------------------------------------------------------------------
// StreamBuffer
// -------------------------------------------------------------------------

StreamBuffer::StreamBuffer(OStream & out)
#if defined(_WIN32)
  : _source(INVALID_HANDLE_VALUE)
#else
  : _source(-1)
#endif
  , _out(out)
  , _size(0)
  , _finished(true) {
  _buffer.resize(1024);
}

void StreamBuffer::setSource(StreamID source) {
#ifndef _WIN32
  _source = source;
  int flags = ::fcntl(_source, F_GETFL, 0);
  if (flags == -1) {
    flags = 0;
  }
  int status = fcntl(_source, F_SETFL, flags | O_NONBLOCK);
  if (status == -1) {
    ::perror("setting stream to non-blocking mode");
    ::exit(-1);
  }
  _finished = false;
#endif
}

bool StreamBuffer::fillBuffer() {
#ifndef _WIN32
  while (!_finished) {
    unsigned avail = _buffer.size() - _size;
    M_ASSERT(_size <= _buffer.size());
    M_ASSERT(avail <= _buffer.size());
    if (avail == 0) {
      return true;
    }
    ssize_t actual = ::read(_source, _buffer.begin() + _size, avail);
    if (actual < 0) {
      if (errno == EWOULDBLOCK) {
        return false;
      }
      ::perror("reading from child processes stream");
      ::abort();
      /// TODO: Set error code.
      _finished = true;
    } else if (actual == 0) {
      // End of stream.
      _finished = true;
    } else {
      _size += actual;
    }
  }
#endif
  return false;
}

bool StreamBuffer::processLines() {
#ifndef _WIN32
  bool more = true;
  while (more && !_finished) {
    more = fillBuffer();
    if (_size == _buffer.size() || _finished) {
      if (_size > 0) {
        unsigned breakPos = _finished ? _size : lastLineBreak();
        _out.write(_buffer.data(), breakPos);
        unsigned remaining = unsigned(_size - breakPos);
        if (remaining > 0) {
          ::memcpy(_buffer.begin(), _buffer.begin() + breakPos, remaining);
        }
        _size = remaining;
      }
    }
  }
#endif
  return _finished;
}

bool StreamBuffer::flush() {
  processLines();
  _out.write(_buffer.data(), _size);
  _size = 0;
  return true;
}

void StreamBuffer::close() {
#ifndef _WIN32
  flush();
  ::close(_source);
  _finished = true;
  _source = -1;
#endif
}

unsigned StreamBuffer::lastLineBreak() {
  unsigned pos = _size;
  while (pos > 0) {
    char ch = _buffer[pos - 1];
    if (ch == '\n' || ch == '\r') {
      break;
    }
    --pos;
  }
  return pos > 0 ? pos : _size;
}

// -------------------------------------------------------------------------
// Process
// -------------------------------------------------------------------------

Process * Process::_processList = NULL;

Process::Process(ProcessListener * listener)
  : _listener(listener)
  , _stdout(console::out())
  , _stderr(console::err())
{
  #if HAVE_UNISTD_H
    _pid = 0;
  #endif
  _next = _processList;
  _processList = this;
}

bool Process::begin(StringRef programName, ArrayRef<StringRef> args, StringRef workingDir) {
  unsigned bufsize = programName.size() + workingDir.size() + 2;
  for (ArrayRef<StringRef>::const_iterator
      it = args.begin(), itEnd = args.end(); it != itEnd; ++it) {
    #if defined(_WIN32)
      int size = ::MultiByteToWideChar(CP_UTF8, 0, it->data(), it->size(), NULL, 0);
      bufsize += size ;
    #else
      bufsize += it->size() + 1;
    #endif
  }

  // Buffer to hold the null-terminated argument strings.
  _commandBuffer.clear();
  _commandBuffer.reserve(bufsize);

  // Buffer to hold the character pointers to arguments.
  _argv.clear();
  _argv.reserve(args.size() + 2);

  // Convert program to a null-terminated string.
  native_char_t * program = appendCommandArg(programName);
  _argv.push_back(program);

  // Convert working dir to a null-terminated string.
  const native_char_t * wdir = appendCommandArg(workingDir);

  // Convert arguments to null-terminated strings.
  for (ArrayRef<StringRef>::const_iterator
      it = args.begin(), itEnd = args.end(); it != itEnd; ++it) {
    _argv.push_back(appendCommandArg(*it));
  }
  _argv.push_back(NULL);

  if (optVerbose) {
    SmallString<0> commandLine;
    commandLine.append(programName);
    for (ArrayRef<StringRef>::const_iterator
        it = args.begin(), itEnd = args.end(); it != itEnd; ++it) {
      commandLine.push_back(' ');
      commandLine.append(*it);
    }
    commandLine.push_back('\n');
    diag::status() << commandLine;
  }

  #if defined(_MSC_VER)
    M_ASSERT(false) << "Implement";
    return true;
#if 0
    bool success = ::CreateProcessW(
      program,
    __inout_opt LPWSTR lpCommandLine,
    __in_opt    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    __in_opt    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    __in        BOOL bInheritHandles,
    __in        DWORD dwCreationFlags,
    __in_opt    LPVOID lpEnvironment,
    wdir,
    __in        LPSTARTUPINFOW lpStartupInfo,
    __out       LPPROCESS_INFORMATION lpProcessInformation
);
#endif
  #elif HAVE_UNISTD_H
    // Create the pipes
    int fdout[2];
    int fderr[2];
    if (::pipe(fdout) == -1) {
      printPosixFileError("executing", programName, errno);
      return false;
    }

    if (::pipe(fderr) == -1) {
      printPosixFileError("executing", programName, errno);
      return false;
    }

    // Spawn the new process
    pid_t pid = ::fork();
    if (pid == 0) {
      // We're the child

      // Close the read pipe
      ::close(fdout[0]);
      ::close(fderr[0]);

      // Assign stdout to our pipe
      if (fdout[1] != STDOUT_FILENO) {
        if (::dup2(fdout[1], STDOUT_FILENO) != STDOUT_FILENO) {
          printPosixFileError("executing", programName, errno);
          ::_exit(-1);
        }
        ::close(fdout[1]);
      }

      // Assign stderr to our pipe
      if (fderr[1] != STDERR_FILENO) {
        if (::dup2(fderr[1], STDERR_FILENO) != STDERR_FILENO) {
          printPosixFileError("executing", programName, errno);
          ::_exit(-1);
        }
        ::close(fderr[1]);
      }

      // Change to working dir
      if (::chdir(wdir) == -1) {
        printPosixFileError("changing directory to", workingDir, errno);
        ::_exit(-1);
      }

      // TODO: Search for program ourselves and control the environment precisely.
      ::execvp(program, _argv.data());
      if (errno == ENOENT) {
        diag::error() << "Program '" << programName << "' not found.";
      } else {
        printPosixFileError("executing", programName, errno);
      }
      ::_exit(-1);
    } else if (pid == -1) {
      printPosixFileError("executing", programName, errno);
      return false;
    } else {
      // We're the parent
      _pid = pid;
      _stdout.setSource(fdout[0]);
      _stderr.setSource(fderr[0]);

      return true;
    }
  #else
    #error Unimplemented: Process::begin()
  #endif
}

native_char_t * Process::appendCommandArg(StringRef arg) {
  native_char_t * result = _commandBuffer.end();
  #if defined(_WIN32)
    int size = ::MultiByteToWideChar(
        CP_UTF8, 0,
        arg.data(), arg.size(),
        _commandBuffer.end(),
        _commandBuffer.capacity() - _commandBuffer.size());
    // TODO: Convert slashes...
    _commandBuffer.resize(_commandBuffer.size() + size);
    _commandBuffer.push_back('\0');
  #else
    _commandBuffer.append(arg.begin(), arg.end());
    _commandBuffer.push_back('\0');
  #endif
  return result;
}

bool Process::processChildIO() {
  _stdout.processLines();
  _stderr.processLines();
  return true;
}

bool Process::cleanup(int status, bool signaled) {
  #if HAVE_UNISTD_H
    _pid = 0;
  #endif
  _stdout.close();
  _stderr.close();
  if (status == 0 && !signaled) {
    if (_listener) {
      _listener->processFinished(*this, true);
      return true;
    }
  } else if (signaled) {
    diag::info() << "Process terminated with signal " << status;
  } else {
    diag::info() << "Process terminated with exit code " << status;
  #if HAVE_UNISTD_H
    SmallString<0> commandLine("  ");
    for (ArrayRef<char *>::const_iterator
        it = _argv.begin(), itEnd = _argv.end(); it != itEnd; ++it) {
      if (*it != NULL) {
        if (it != _argv.begin()) {
          commandLine.push_back(' ');
        }
        commandLine.append(*it);
      }
    }
    commandLine.push_back('\n');
    diag::status() << commandLine;
  #endif
  }

  if (_listener) {
    _listener->processFinished(*this, false);
  }
  return false;
}

bool Process::waitForProcessEvent() {
  #if HAVE_UNISTD_H
  // See if there are even any processes, don't wait otherwise
  int runningCount = 0;
  SmallVector<struct pollfd, 16> fds;
  for (Process * p = _processList; p != NULL; p = p->_next) {
    if (p->_pid != 0) {
      ++runningCount;
    }
  }
  if (runningCount == 0) {
    diag::info() << "No processes running";
    return true;
  }

  fds.resize(runningCount * 2);
  unsigned index = 0;
  for (Process * p = _processList; p != NULL; p = p->_next) {
    if (p->_pid != 0) {
      fds[index].fd = p->_stdout.source();
      fds[index].events = POLLIN;
      fds[index].revents = 0;
      ++index;
      fds[index].fd = p->_stderr.source();
      fds[index].events = POLLIN;
      fds[index].revents = 0;
      ++index;
    }
  }

  int status = ::poll(fds.data(), fds.size(), 100);
  if (status < 0) {
    perror("wait for child process");
    return false;
  }

  if (status > 0) {
    index = 0;
    for (Process * p = _processList; p != NULL; p = p->_next) {
      if (p->_pid != 0) {
        short revOut = fds[index].revents;
        if (revOut & POLLIN) {
          p->_stdout.processLines();
        }
        ++index;
        short revErr = fds[index].revents;
        if (revErr & POLLIN) {
          p->_stderr.flush();
        }
        ++index;
      }
    }
  }

  while (runningCount > 0) {
    pid_t id = ::waitpid(-1, &status, WNOHANG);
    if (id < 0) {
      if (errno == EINTR) {
        diag::warn() << "::wait() interrupted";
        return false;
      }
      if (errno == ECHILD) {
        //diag::warn() << "::wait() reported no child processes";
        //abort();
        return true;
      }
      perror("wait for child process");
      M_ASSERT(false) << "Unexpected error code from call to wait()";
    } else if (id == 0) {
      return true;
    }

    for (Process * p = _processList; p != NULL; p = p->_next) {
      if (p->_pid == id) {
        if (WIFEXITED(status)) {
          return p->cleanup(WEXITSTATUS(status), false);
        } else if (WIFSIGNALED(status)) {
          return p->cleanup(WTERMSIG(status), true);
        } else {
          M_ASSERT(false) << "Invalid result from call to wait()";
        }
      }
    }
  }

  #endif
  return true;
}

}

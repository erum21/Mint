/* ================================================================ *
   Mint
 * ================================================================ */

#ifndef MINT_SUPPORT_PROCESS_H
#define MINT_SUPPORT_PROCESS_H

#ifndef MINT_COLLECTIONS_SMALLSTRING_H
#include "mint/collections/SmallString.h"
#endif

#ifndef MINT_COLLECTIONS_ARRAYREF_H
#include "mint/collections/ArrayRef.h"
#endif

#ifndef MINT_SUPPORT_PATH_H
#include "mint/support/Path.h"
#endif

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace mint {

class ProcessListener;
class OStream;

/** -------------------------------------------------------------------------
    Used to buffer the output of a stream from a child process. This makes
    sure that the output isn't garbled by insuring that only complete lines
    get written.
 */
class StreamBuffer {
public:
  #if defined(_WIN32)
    typedef HANDLE StreamID;
  #else
    typedef int StreamID;
  #endif

  StreamBuffer(OStream & out);

  /// Return the identifier of the input pipe.
  StreamID source() const { return _source; }

  /// Set the source I/O handle for the buffered stream. This also sets the source
  /// to be non-blocking.
  void setSource(StreamID source);

  /// Process as many lines as possible, until there is no more data available
  /// on the input source at this time. Returns true if we've reached the end of
  /// the input.
  bool processLines();

  bool flush();

  void close();

private:
  /// Fill the buffer from the source. Return true if the buffer is actually
  /// full, return false if we couldn't fill the buffer for any reason (generally
  /// meaning that there's no more data available at this moment.)
  bool fillBuffer();

  /// Return the offset of the last line break, or end of buffer if there
  /// was no line break.
  unsigned lastLineBreak();

  StreamID _source;
  OStream & _out;
  unsigned _size;
  SmallString<0> _buffer;
  bool _finished;
};

/** -------------------------------------------------------------------------
    A class which can be used to spawn processes that run commands.
 */
class Process {
public:

  /// Constructor
  Process(ProcessListener * listener);

  /// Run a command as a subprocess.
  bool begin(StringRef programName, ArrayRef<StringRef> args, StringRef workingDir);

  /// Process I/O from the child process.
  bool processChildIO();

  /// Wait for a process to exit.
  static bool waitForProcessEvent();

private:
  bool cleanup(int status, bool signaled);
  native_char_t * appendCommandArg(StringRef arg);

  static Process * _processList;

  ProcessListener * _listener;
  Process * _next;

  SmallVector<native_char_t, 0> _commandBuffer;
  SmallVector<native_char_t *, 32> _argv;

  StreamBuffer _stdout;
  StreamBuffer _stderr;

  #if HAVE_UNISTD_H
    pid_t _pid;
  #endif
};

/** -------------------------------------------------------------------------
    Interface used to listen for processes being finished.
 */
class ProcessListener {
public:
  virtual void processFinished(Process & process, bool success) = 0;
};

} // namespace

#endif // MINT_SUPPORT_PROCESS_H

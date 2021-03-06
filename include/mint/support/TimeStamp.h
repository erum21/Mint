/* ================================================================ *
   Represents a buffer containing lines of text.
 * ================================================================ */

#ifndef MINT_SUPPORT_TIMESTAMP_H
#define MINT_SUPPORT_TIMESTAMP_H

#ifndef MINT_CONFIG_H
#include "mint/config.h"
#endif

#if HAVE_TIME_H
#include <time.h>
#endif

namespace mint {

/** -------------------------------------------------------------------------
    Represents the last modified time of a file.
 */
#if HAVE_TYPE_TIMESPEC
class TimeStamp {
public:
  typedef const char * iterator;
  typedef const char * const_iterator;

  /// Constructor
  TimeStamp() {
    _value.tv_sec = 0;
    _value.tv_nsec = 0;
  }

  TimeStamp(struct timespec & ts) {
    _value.tv_sec = ts.tv_sec;
    _value.tv_nsec = ts.tv_nsec;
  }

  TimeStamp(time_t time) {
    _value.tv_sec = time;
    _value.tv_nsec = 0;
  }

  TimeStamp(const TimeStamp  & ts) {
    _value.tv_sec = ts._value.tv_sec;
    _value.tv_nsec = ts._value.tv_nsec;
  }

  const TimeStamp & operator=(const TimeStamp  & ts) {
    _value.tv_sec = ts._value.tv_sec;
    _value.tv_nsec = ts._value.tv_nsec;
    return *this;
  }

  bool operator==(const TimeStamp  & ts) const {
    return _value.tv_sec == ts._value.tv_sec && _value.tv_nsec == ts._value.tv_nsec;
  }

  bool operator!=(const TimeStamp  & ts) const {
    return !this->operator==(ts);
  }

  bool operator<(const TimeStamp  & ts) const {
    if (_value.tv_sec < ts._value.tv_sec) {
      return true;
    } else if (_value.tv_sec > ts._value.tv_sec) {
      return false;
    } else {
      return _value.tv_nsec < ts._value.tv_nsec;
    }
  }

  bool operator<=(const TimeStamp  & ts) {
    if (_value.tv_sec < ts._value.tv_sec) {
      return true;
    } else if (_value.tv_sec > ts._value.tv_sec) {
      return false;
    } else {
      return _value.tv_nsec <= ts._value.tv_nsec;
    }
  }

private:
  struct timespec _value;
};
#elif HAVE_TYPE_TIME_T
class TimeStamp {
public:
  typedef const char * iterator;
  typedef const char * const_iterator;

  /// Constructor
  TimeStamp() {
    _value = 0;
  }

  TimeStamp(time_t time) {
    _value = time;
  }

  TimeStamp(const TimeStamp  & ts) {
    _value = ts._value;
  }

  const TimeStamp & operator=(const TimeStamp  & ts) {
    _value = ts._value;
    return *this;
  }

  bool operator==(const TimeStamp  & ts) const {
    return _value == ts._value;
  }

  bool operator!=(const TimeStamp  & ts) const {
    return !this->operator==(ts);
  }

  bool operator<(const TimeStamp  & ts) const {
    return _value < ts._value;
  }

  bool operator<=(const TimeStamp  & ts) {
    return _value <= ts._value;
  }

private:
  time_t _value;
};
#else
#error Unimplemented: TimeStamp
#endif

} // namespace

#endif // MINT_SUPPORT_TEXTBUFFER_H

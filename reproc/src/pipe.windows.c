#ifndef _WIN32_WINNT
  #define _WIN32_WINNT 0x0600 // _WIN32_WINNT_VISTA
#elif _WIN32_WINNT < 0x0600
  #error "_WIN32_WINNT must be greater than _WIN32_WINNT_VISTA (0x0600)"
#endif

#include "pipe.h"

#include <limits.h>
#include <stdlib.h>
#include <windows.h>

#include "error.h"
#include "handle.h"
#include "macro.h"

const HANDLE PIPE_INVALID = INVALID_HANDLE_VALUE; // NOLINT

const short PIPE_EVENT_IN = 1;
const short PIPE_EVENT_OUT = 2;

int pipe_init(HANDLE *read, HANDLE *write)
{
  ASSERT(read);
  ASSERT(write);

  HANDLE pair[] = { PIPE_INVALID, PIPE_INVALID };
  int r = -1;

  SECURITY_ATTRIBUTES sa = {
    .nLength = sizeof(SECURITY_ATTRIBUTES),
    .bInheritHandle = FALSE,
    .lpSecurityDescriptor = NULL
  };

  if (!CreatePipe(&pair[0], &pair[1], &sa, 0)) {
    r = -(int) GetLastError();
    goto finish;
  }

  *read = pair[0];
  *write = pair[1];

  pair[0] = PIPE_INVALID;
  pair[1] = PIPE_INVALID;
  r = 0;

finish:
  handle_destroy(pair[0]);
  handle_destroy(pair[1]);

  return r;
}

int pipe_nonblocking(HANDLE pipe, bool enable)
{
  DWORD mode = enable ? PIPE_NOWAIT : PIPE_WAIT;
  BOOL r = SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
  return r ? 0 : -(int) GetLastError();
}

int pipe_read(HANDLE pipe, uint8_t *buffer, size_t size)
{
  ASSERT(pipe != PIPE_INVALID);
  ASSERT(buffer);
  ASSERT(size <= INT_MAX);

  DWORD bytes_read = 0;
  BOOL r = ReadFile(pipe, buffer, (DWORD) size, &bytes_read, NULL);

  if (!r) {
    DWORD err = GetLastError();

    if (err == ERROR_BROKEN_PIPE) {
      return -ERROR_BROKEN_PIPE;
    }

    if (err == ERROR_NO_DATA) {
      return -ERROR_NO_DATA;
    }

    return -(int) err;
  }

  if (bytes_read == 0) {
    return -ERROR_BROKEN_PIPE;
  }

  return (int) bytes_read;
}

int pipe_write(HANDLE pipe, const uint8_t *buffer, size_t size)
{
  ASSERT(pipe != PIPE_INVALID);
  ASSERT(buffer);
  ASSERT(size <= INT_MAX);

  DWORD bytes_written = 0;
  BOOL r = WriteFile(pipe, buffer, (DWORD) size, &bytes_written, NULL);

  if (!r) {
    DWORD err = GetLastError();

    if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
      return -ERROR_BROKEN_PIPE;
    }

    return -(int) err;
  }

  return (int) bytes_written;
}

int pipe_poll(pipe_event_source *sources, size_t num_sources, int timeout)
{
  ASSERT(num_sources <= INT_MAX);

  DWORD start = GetTickCount();

  for (;;) {
    int ready = 0;

    for (size_t i = 0; i < num_sources; i++) {
      sources[i].events = 0;

      if (sources[i].pipe == PIPE_INVALID) {
        continue;
      }

      if (sources[i].interests & PIPE_EVENT_IN) {
        DWORD avail = 0;
        BOOL r = PeekNamedPipe(sources[i].pipe, NULL, 0, NULL, &avail, NULL);

        if (r && avail > 0) {
          sources[i].events |= PIPE_EVENT_IN;
        } else if (!r) {
          // Pipe broken (write end closed). Signal as readable so the
          // caller's subsequent read returns EPIPE.
          sources[i].events |= PIPE_EVENT_IN;
        }
      }

      if (sources[i].interests & PIPE_EVENT_OUT) {
        // Write end of a pipe is generally always writable.
        sources[i].events |= PIPE_EVENT_OUT;
      }

      if (sources[i].events) {
        ready++;
      }
    }

    if (ready > 0) {
      return ready;
    }

    if (timeout == 0) {
      return 0;
    }

    DWORD elapsed = GetTickCount() - start;
    if (timeout > 0 && (int) elapsed >= timeout) {
      return 0;
    }

    Sleep(1);
  }
}

int pipe_shutdown(HANDLE pipe)
{
  (void) pipe;
  return 0;
}

HANDLE pipe_destroy(HANDLE pipe)
{
  return handle_destroy(pipe);
}

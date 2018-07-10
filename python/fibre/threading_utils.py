"""
Python threading primitives are severely limited, mostly because it's not
possible to wait on multiple events simultaneously.
This module provides substitutes for some of the Python builtins.

EventWaitHandle -- once triggered, a EventWaitHandle remains set until it is explicitly cleared
Semaphore -- as commonly defined
"""

import os
import sys
import time
import threading
import selectors
from fibre.utils import OperationAbortedError

import abc
if sys.version_info >= (3, 4):
    ABC = abc.ABC
else:
    ABC = abc.ABCMeta('ABC', (), {})


class Waitable(ABC):
    @abc.abstractmethod
    def try_acquire(self):
        """
        Shall acquire the underlying wait-resource in a thread safe way.
        Return True if the resource could be acquired and False otherwise.
        """
        pass
    @abc.abstractmethod
    def fileno(self):
        """Shall return an OS file descriptor that can be used with selectors"""
        pass


class EventWaitHandle(Waitable):
    """
    Alternative to threading.EventWaitHandle(), enhanced by the subscribe() function
    that the original fails to provide.
    @param Trigger: if supplied, the newly created event will be triggered
                    as soon as the trigger event becomes set
    """
    def __init__(self, name=None, trigger=None, auto_reset=False):
        self.name = name
        self._read_fd, self._write_fd = os.pipe()
        os.set_blocking(self._read_fd, False)
        self._is_set = False
        self._subscribers = []
        self._lock = threading.Lock()
        self._auto_reset = auto_reset
        if not trigger is None:
            trigger.subscribe(lambda: self.set("subscription"))

    def try_acquire(self):
        """for internal use by wait_any"""
        # This wait handle is never reset
        with self._lock:
            if self._is_set:
                if self._auto_reset:
                    os.read(self._read_fd, 1)
                    self._is_set = False
                return True
            else:
                return False

    def fileno(self):
        """for internal use by wait_any"""
        return self._read_fd

    def set(self, reason="unknown"):
        """
        Sets the event and invokes all subscribers if the event was
        not already set
        """
        #print("set because {}".format(reason))
        with self._lock:
            if not self._is_set:
                self._is_set = True
                #os.set_blocking(self._read_fd, False)
                os.write(self._write_fd, b'1')
                for s in self._subscribers:
                    s()
    def is_set(self):
        return self._is_set

    def clear(self):
        """Clears the event if it is set. This function returns immediately"""
        with self._lock:
            if self._is_set:
                os.read(self._read_fd, 1)
                self._is_set = False

    def wait(self, timeout=None, cancellation_token=None):
        wait_any(self, timeout=timeout, cancellation_token=cancellation_token)
        #rfds, wfds, efds = select.select([self._read_fd], [], [], timeout)
        #return self._read_fd in rfds

    def subscribe(self, handler):
        """
        Invokes the specified handler exactly once each time the
        event is set. If the event is already set, the
        handler is invoked immediately.
        Returns the handler that was passed as an argument
        """
        if handler is None:
            raise TypeError
        with self._lock:
            self._subscribers.append(handler)
            if self._is_set():
                handler()
        return handler
    
    def unsubscribe(self, handler):
        with self._lock:
            self._subscribers.pop(self._subscribers.index(handler))

    def trigger_after(self, timeout):
        """
        Triggers the event after the specified timeout.
        This function returns immediately.
        """
        def delayed_trigger():
            if not self.wait(timeout=timeout):
                self.set()
        threading.Thread(name='trigger_after', target=delayed_trigger, daemon=True).start()

    def __del__(self):
        os.close(self._read_fd)
        os.close(self._write_fd)

class Semaphore(Waitable):
    def __init__(self, name=None, count=0):
        self.name = name
        self._read_fd, self._write_fd = os.pipe()
        os.set_blocking(self._read_fd, False)
        self._count = count
        self._lock = threading.Lock()

    def get_count(self):
        with self._lock:
            return self._count

    def try_acquire(self):
        """for internal use by wait_any"""
        # This wait handle is never reset
        with self._lock:
            if self._count <= 0:
                return False
            else:
                os.read(self._read_fd, 1)
                self._count -= 1
                return True

    def fileno(self):
        """for internal use by wait_any"""
        return self._read_fd

    def release(self, reason="unknown"):
        """
        Sets the event and invokes all subscribers if the event was
        not already set
        """
        #print("released because {}".format(reason))
        with self._lock:
            self._count += 1
            os.write(self._write_fd, b'1')

    def acquire(self, timeout=None, cancellation_token=None):
        wait_any(self, timeout=timeout, cancellation_token=cancellation_token)
    
    def unsubscribe(self, handler):
        with self._lock:
            self._subscribers.pop(self._subscribers.index(handler))

    def trigger_after(self, timeout):
        """
        Triggers the event after the specified timeout.
        This function returns immediately.
        """
        def delayed_trigger():
            if not self.wait(timeout=timeout):
                self.set()
        threading.Thread(name='trigger_after', target=delayed_trigger, daemon=True).start()

    def __del__(self):
        os.close(self._read_fd)
        os.close(self._write_fd)



def wait_any(*events, **kwargs):
    """
    Blocks until any of the specified events are triggered.
    Returns the zero-based index of the event that was triggerd.

    Keyword arguments:
    timeout -- Specifies a timeout in seconds. If no event triggers
    within this time, the function raises a TimeoutError.
    If None, the timeout is infinite. (default None)
    cancellation_token -- If triggered before any of the other events
    are triggered and before a timeout occurs, the function raises
    an OperationAbortedError. (default None)
    """
    timeout = kwargs.pop('timeout', None)
    cancellation_token = kwargs.pop('cancellation_token', None)
    if kwargs:
        raise TypeError("unknown keyword argument {}".format(kwargs[0][0]))

    deadline = None if timeout is None else (time.monotonic() + timeout)

    if cancellation_token:
        events += (cancellation_token,)
        cancellation_token_idx = len(events) - 1
    else:
        cancellation_token_idx = None

    # Once an event has triggered at the low level, all threads that wait on this event
    # are unblocked at the same time. However some events (e.g. Semaphores) only allow
    # for a limited number of waits to pass, so we still need to try to actually acquire
    # one of the triggered events. If this doesn't succeed, we ignore the spurious
    # wake-up and continue waiting.
    while True:
        with selectors.SelectSelector() as sel:
            for (i, evt) in enumerate(events):
                if not isinstance(evt, Waitable):
                    raise TypeError("This function only supports events that inherit from " + str(Waitable))
                sel.register(evt.fileno(), selectors.EVENT_READ, i)
            timeout = None if deadline is None else max(deadline - time.monotonic(), 0)
            result = sel.select(timeout=timeout)

        if not result:
            raise TimeoutError

        for (key, _) in result:
            event_idx = key.data
            if events[event_idx].try_acquire():
                if event_idx == cancellation_token_idx:
                    raise OperationAbortedError
                else:
                    return event_idx

    if len(result) != 1:
        raise Exception("multiple triggered: " + str(result))

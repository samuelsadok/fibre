
import time
import threading

import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
from fibre.threading_utils import EventWaitHandle, Semaphore, wait_any
from fibre.utils import OperationAbortedError

def invoke_delayed(fn, delay):
    def set_event_thread():
        time.sleep(delay)
        fn()
    threading.Thread(target=set_event_thread).start()

def expect_exception(fn, exception):
    try:
        fn()
    except exception:
        pass
    else:
        raise Exception("should have timed out")

#evt2 = EventWaitHandle()
#



def test_wait_handle(auto_reset_handle):
    evt = EventWaitHandle(name="test-handle", auto_reset=auto_reset_handle)

    print("testing wait timeout...")
    expect_exception(lambda: evt.wait(timeout=0), TimeoutError)
    expect_exception(lambda: evt.wait(timeout=1), TimeoutError)

    print("testing trigger...")
    invoke_delayed(evt.set, 1)
    assert(not evt.is_set())
    evt.wait(timeout=2)
    if not auto_reset_handle:
        assert(evt.is_set())
        evt.wait(timeout=0)
        evt.clear()

    assert(not evt.is_set())
    expect_exception(lambda: evt.wait(timeout=0), TimeoutError)

    print("test cancellation token...")
    cancellation_token = EventWaitHandle("cancellation token")
    invoke_delayed(cancellation_token.set, 2)
    expect_exception(
        lambda: evt.wait(cancellation_token=cancellation_token, timeout=1),
        TimeoutError)
    expect_exception(
        lambda: evt.wait(cancellation_token=cancellation_token, timeout=2),
        OperationAbortedError)

def test_auto_reset_event_handle():
    evt = EventWaitHandle(name="test-handle")

    print("testing wait timeout...")
    expect_exception(lambda: evt.wait(timeout=0), TimeoutError)
    expect_exception(lambda: evt.wait(timeout=1), TimeoutError)

    print("testing trigger...")
    invoke_delayed(evt.set, 1)
    assert(not evt.is_set())
    evt.wait(timeout=2)
    assert(evt.is_set())
    evt.wait(timeout=0)

    evt.clear()
    assert(not evt.is_set())

    print("test cancellation token...")
    cancellation_token = EventWaitHandle("cancellation token")
    invoke_delayed(cancellation_token.set, 2)
    expect_exception(
        lambda: evt.wait(cancellation_token=cancellation_token, timeout=1),
        TimeoutError)
    expect_exception(
        lambda: evt.wait(cancellation_token=cancellation_token, timeout=2),
        OperationAbortedError)

def test_semaphore():
    sem = Semaphore(name="test-semaphore", count=0)

    print("testing acquire timeout...")
    expect_exception(lambda: sem.acquire(timeout=0), TimeoutError)
    expect_exception(lambda: sem.acquire(timeout=1), TimeoutError)

    print("testing simple release-aquire...")
    invoke_delayed(sem.release, 1) # release twice
    assert(sem.get_count() == 0)
    sem.acquire(timeout=2)
    assert(sem.get_count() == 0)
    
    print("testing releasing/acquiring multiple times...")
    sem.release()
    sem.release()
    assert(sem.get_count() == 2)
    sem.acquire(timeout=0)
    assert(sem.get_count() == 1)
    sem.acquire(timeout=0)
    assert(sem.get_count() == 0)
    expect_exception(lambda: sem.acquire(timeout=0), TimeoutError)

    print("test cancellation token...")
    cancellation_token = EventWaitHandle("cancellation token")
    invoke_delayed(cancellation_token.set, 2)
    expect_exception(
        lambda: sem.acquire(cancellation_token=cancellation_token, timeout=1),
        TimeoutError)
    expect_exception(
        lambda: sem.acquire(cancellation_token=cancellation_token, timeout=2),
        OperationAbortedError)

    print("testing acquire from multiple threads...")
    sem2 = Semaphore(name='test-semaphore-2')
    successful_acquisitions = []
    def try_acquire():
        try:
            idx = wait_any(sem, sem2, timeout=2)
        except TimeoutError:
            pass
        else:
            successful_acquisitions.append(idx) # atomic
    # Acquire on 10 threads
    threads = []
    for _ in range(10):
        t = threading.Thread(target=try_acquire)
        threads.append(t)
        t.start()
    # Release from 2 threads
    for _ in range(2):
        invoke_delayed(sem.release, 1)
    for _ in range(3):
        invoke_delayed(sem2.release, 1)
    # Wait until done, then count how many threads could acquire
    for t in threads:
        t.join()
    assert(successful_acquisitions.count(0) == 2)
    assert(successful_acquisitions.count(1) == 3)
    assert(len(successful_acquisitions) == 5)

test_wait_handle(False)
test_wait_handle(True)
test_semaphore()
print("All tests succeeded!")


import bisect

class IntervalList(object):
    """
    Allows assigning values to intervals of numbers.
    """

    def __init__(self):
        self._ends = [] # Sorted list that stores the end + 1 of each interval
        self._values = [] # Stores the value for each interval

        # Example:
        #   self._ends == [2, 4, 10]
        #   self._values == [a, b, c]
        # means:
        #   [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
        #   [a, a, b, b, c, c, c, c, c, c]
    
    def _get_interval_index(self, pos):
        """Returns the index of the interval that includes the specified position"""
        idx = bisect.bisect_right(self._ends, pos)
        if idx >= len(self._ends):
            self._ends.append(pos + 1)
            self._values.append(None)
        return idx
    
    def _get_start_and_end(self, idx):
        start = self._ends[idx - 1] if idx >= 1 else 0
        end = self._ends[idx] if idx < len(self._ends) else None
        return start, end

    def set_value(self, offset, length, value):
        if length <= 0:
            return
        
        # split in the beginning
        idx1 = self._get_interval_index(offset)
        start, end = self._get_start_and_end(idx1)
        if start != offset and self._values[idx1] != value:
            self._ends.insert(idx1, offset)
            self._values.insert(idx1 + 1, None)
            idx1 += 1

        # split in the end
        idx2 = self._get_interval_index(offset + length - 1)
        start, end = self._get_start_and_end(idx2)
        if offset + length != end and self._values[idx2] != value:
            self._ends.insert(idx2, offset + length)
            self._values.insert(idx2, None)

        # set new value
        self._values[idx2] = value
        
        # check if preceeding or succeeding intervals have the same value
        if idx1 > 0 and self._values[idx1 - 1] == value:
            idx1 -= 1
        if idx2 + 1 < len(self._values) and self._values[idx2 + 1] == value:
            idx2 += 1

        # coalesce all consecutive intervals with the same value
        if idx1 != idx2:
            self._ends = self._ends[:idx1] + self._ends[idx2:]
            self._values = self._values[:idx1] + self._values[idx2:]
            idx2 = idx1

    def sanity_check(self):
        assert(sorted(self._ends) == self._ends) # check if sorted
        assert(len(self._ends) == len(self._values))
        if len(self._ends) >= 2: # check if all elements in self._ends are unique
            assert(not any([self._ends[idx] == self._ends[idx+1] for idx in range(len(self._ends)-1)]))
        if len(self._values) >= 2: # check that there are no consecutive equal valued intervals
            assert(not any([self._values[idx] == self._values[idx+1] for idx in range(len(self._values)-1)]))

    def get_intervals(self, offset = 0, length = None):
        """
        Returns all intervals within the specified range.
        
        length == None means "until the end of the interval list"
        """
        assert(offset >= 0)
        
        idx1 = self._get_interval_index(offset)
        if length is None:
            idx2 = len(self._ends) - 1 # self._ends is guaranteed to have at least 1 element here
            length = self._ends[idx2] - offset
        else:
            assert(length >= 0)
            idx2 = self._get_interval_index(offset + length - 1)

        if length == 0:
            return

        pos = offset
        for idx, end in enumerate(self._ends[idx1:idx2], idx1):
            yield (pos, end - pos, self._values[idx])
            pos = end

        yield (pos, offset + length - pos, self._values[idx2])

    def __iter__(self):
        return self.get_intervals()
        #pos = 0
        #for idx, end in enumerate(self._ends):
        #    yield (pos, end - pos, self._values[idx])
        #    pos = end

def test_interval_list():
    lst = IntervalList()

    lst.set_value(4, 10, "a")
    lst.set_value(2, 6, "b")
    lst.set_value(10, 10, "c")
    lst.sanity_check()
    expected_intervals = [(0, 2, None), (2, 6, "b"), (8, 2, "a"), (10, 10, "c")]
    assert(list(lst) == expected_intervals)

    lst.set_value(0, 2, "b")
    lst.set_value(8, 2, "c")
    lst.sanity_check()
    expected_intervals = [(0, 8, "b"), (8, 12, "c")]
    assert(list(lst) == expected_intervals)

    lst.set_value(8, 12, "a")
    lst.set_value(20, 2, "b")
    lst.set_value(22, 3, "b")
    lst.set_value(25, 5, "a")
    lst.set_value(20, 5, "a")
    lst.sanity_check()
    expected_intervals = [(0, 8, 'b'), (8, 22, 'a')]
    assert(list(lst) == expected_intervals)

    expected_intervals = [(5, 3, 'b'), (8, 22, 'a')]
    assert(list(lst.get_intervals(5, None)) == expected_intervals)
    expected_intervals = [(5, 3, 'b'), (8, 17, 'a')]
    assert(list(lst.get_intervals(5, 20)) == expected_intervals)
    expected_intervals = [(8, 22, 'a')]
    assert(list(lst.get_intervals(8, 22)) == expected_intervals)


"""Full-resolution waveform rows backed by packed doubles, with zero-copy views."""
from array import array
import csv
import math


class Rows:
    def __init__(self, data, width, indices=None, stride=None):
        self.data = data
        self.width = width
        self.stride = width if stride is None else stride
        self.indices = range(len(data) // self.stride) if indices is None else indices

    def __len__(self):
        return len(self.indices)

    def __getitem__(self, index):
        if isinstance(index, slice):
            return Rows(self.data, self.width, self.indices[index], self.stride)
        start = self.indices[index] * self.stride
        return self.data[start:start + self.width]

    def project(self, width):
        if not 0 < width <= self.width:
            raise ValueError('invalid waveform projection width')
        return Rows(self.data, width, self.indices, self.stride)


class Projection:
    def __init__(self, rows, width, indices=None):
        self.rows, self.width = rows, width
        self.indices = range(len(rows)) if indices is None else indices

    def __len__(self):
        return len(self.indices)

    def __getitem__(self, index):
        if isinstance(index, slice):
            return Projection(self.rows, self.width, self.indices[index])
        return self.rows[self.indices[index]][:self.width]


def project(rows, width):
    return rows.project(width) if isinstance(rows, Rows) else Projection(rows, width)


def checked_rows(rows, width, label):
    if width < 2:
        raise ValueError('waveform needs time and at least one observation')
    data = array('d')
    previous = None
    count = 0
    for row in rows:
        if len(row) != width:
            raise ValueError(f'{label}: expected {width} columns, got {len(row)}')
        values = tuple(map(float, row))
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f'{label}: nonfinite or missing sample')
        if previous is not None and values[0] <= previous:
            raise ValueError(f'{label}: non-increasing time')
        data.extend(values)
        previous = values[0]
        count += 1
    if count < 10:
        raise ValueError(f'{label}: incomplete trajectory')
    return Rows(memoryview(data).toreadonly(), width)


def read_cpp(path):
    with path.open(newline='') as stream:
        reader = csv.reader(stream)
        header = next(reader)
        return header, checked_rows(reader, len(header), str(path))


def read_spice(path, header):
    with path.open() as stream:
        names = next(stream).split()
        if len(names) != len(header) or names[0] != 'time':
            raise ValueError('ngspice output header does not match electrical observation count')
        return checked_rows((line.split() for line in stream if line.strip()), len(header), str(path))

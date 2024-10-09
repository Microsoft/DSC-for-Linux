import ctypes
import struct
import sys
import time


MI_BOOLEAN = 0
MI_UINT8 = 1
MI_SINT8 = 2
MI_UINT16 = 3
MI_SINT16 = 4
MI_UINT32 = 5
MI_SINT32 = 6
MI_UINT64 = 7
MI_SINT64 = 8
MI_REAL32 = 9
MI_REAL64 = 10
MI_CHAR16 = 11
MI_DATETIME = 12
MI_STRING = 13
# MI_REFERENCE = 14
MI_INSTANCE = 15
MI_BOOLEANA = 16
MI_UINT8A = 17
MI_SINT8A = 18
MI_UINT16A = 19
MI_SINT16A = 20
MI_UINT32A = 21
MI_SINT32A = 22
MI_UINT64A = 23
MI_SINT64A = 24
MI_REAL32A = 25
MI_REAL64A = 26
MI_CHAR16A = 27
MI_DATETIMEA = 28
MI_STRINGA = 29
# MI_REFERENCEA = 30
MI_INSTANCEA = 31

MI_NULL_FLAG = 64

MI_TYPE_NAMES={
0:'MI_BOOLEAN',
1:'MI_UINT8',
2:'MI_SINT8',
3:'MI_UINT16',
4:'MI_SINT16',
5:'MI_UINT32',
6:'MI_SINT32',
7:'MI_UINT64',
8:'MI_SINT64',
9:'MI_REAL32',
10:'MI_REAL64',
11:'MI_CHAR16',
12:'MI_DATETIME',
13:'MI_STRING',
14:'MI_REFERENCE',
15:'MI_INSTANCE',
16:'MI_BOOLEANA',
17:'MI_UINT8A',
18:'MI_SINT8A',
19:'MI_UINT16A',
20:'MI_SINT16A',
21:'MI_UINT32A',
22:'MI_SINT32A',
23:'MI_UINT64A',
24:'MI_SINT64A',
25:'MI_REAL32A',
26:'MI_REAL64A',
27:'MI_CHAR16A',
28:'MI_DATETIMEA',
29:'MI_STRINGA',
30:'MI_REFERENCEA',
31:'MI_INSTANCEA',
64:'MI_NULL_FLAG',
}

DO_TRACE = True
DO_VERBOSE_TRACE = False


def trace(text):
    if DO_TRACE:
        sys.stdout.write(repr(text) + '\n')


def verbose_trace(text):
    if DO_VERBOSE_TRACE:
        trace(text)


def read_string(fd):
    verbose_trace('<read_string>')
    buf = fd.recv(4)
    strl = struct.unpack('@i', buf)[0]
    verbose_trace('  len: ' + str(strl))
    text = ''
    if 0 < strl:
        buf = fd.recv(strl)
        text = buf.decode('utf8')
    verbose_trace('  str: "' + text + '"')
    verbose_trace('</read_string>')
    return text


def read_arg_name(fd):
    verbose_trace('<read_argname>')
    name = read_string(fd)
    if sys.version < '2.6':
        arg_name = name.encode('ascii', 'ignore')
    else:
        arg_name = name
    verbose_trace('  arg_name: "' + arg_name + '"')
    verbose_trace('</read_argname>')
    return arg_name


def read_values(fd):
    verbose_trace('<read_values>')
    arg_dict = dict()
    buf = fd.recv(4)
    argc = struct.unpack('@i', buf)[0]
    verbose_trace('  argc: ' + str(argc))
    for _ in range(argc):
        arg_name = read_arg_name(fd)
        arg_val = MI_Value.read(fd)
        arg_dict[arg_name] = arg_val
    verbose_trace('</read_values>')
    return arg_dict


def write_string(fd, st):
    verbose_trace('<write_string>')
    verbose_trace('  st: "' + st + '"')
    verbose_trace(st)
    buf = struct.pack('@i', len(st))
    if not isinstance(buf, str):
        buf += bytes(st, 'utf8')
    else:
        buf += st
    fd.send(buf)
    verbose_trace('</write_string>')


def write_values(fd, d):
    trace('<write_values>')
    buf = struct.pack('@i', len(d))
    fd.send(buf)
    verbose_trace('  len: ' + str(len(d)))
    if sys.version > '2.9':
        for key, value in d.items():
            verbose_trace('  key: ' + key)
            if not hasattr(value, 'value'):
                sys.stderr.write('\n  key: ' + key + ' is not mi_value\n')
            verbose_trace('  value: ' + str(value.value))
            if value.value is not None:
                write_string(fd, key)
                value.write(fd)
    else:
        for key, value in d.iteritems():
            trace('  key: ' + key)
            verbose_trace('  value: ' + repr(value.value))
            if value is not None:
                verbose_trace('  writing value')
                write_string(fd, key)
                value.write(fd)
            else:
                verbose_trace('  not writing value')
    trace('</write_values>')


class File_desc:
    def sendall(self, buf):
        pass


class MI_Value:
    value = None

    def __init__(self, type):
        self.type = type

    def __repr__(self):
        return MI_TYPE_NAMES[self.type]+': '+repr(self.value)

    def __eq__(self, other):
        if not isinstance(other,MI_Value):
            return False
        if self.type != other.type:
            return False
        if type(self.value) != list :
            if hasattr(self.value,'value'):
                return self.value.value == other.value.value
            return self.value == other.value
        else:
            if len(self.value) != len(other.value):
                return False
            for index in range(len(self.value)):
                if self.value[index] != other.value[index]:
                    return False
            return True

    def __ne__(self, other):
        if self.__eq__(self,other):
            return False
        return True

    def write(self, fd):
        verbose_trace('  <MI_Value::write>')
        val = self.type
        if self.value is None:
            val = val | MI_NULL_FLAG
        verbose_trace('    type: ' + str(self.type) + ' ' + repr(self.value))
        buf = struct.pack('@B', val)
        fd.sendall(buf)
        verbose_trace('  </MI_Value::write>')

    @staticmethod
    def read(fd):
        verbose_trace('<MI_Value::read>')
        buf = fd.recv(1)
        type = struct.unpack('@B', buf)[0]
        switch = type & ~(MI_NULL_FLAG)
        verbose_trace('  type: ' + str(switch))
        val = None
        if MI_BOOLEAN == switch:
            val = MI_Boolean.read(fd, type)
        elif MI_UINT8 == switch:
            val = MI_Uint8.read(fd, type)
        elif MI_SINT8 == switch:
            val = MI_Sint8.read(fd, type)
        elif MI_UINT16 == switch:
            val = MI_Uint16.read(fd, type)
        elif MI_SINT16 == switch:
            val = MI_Sint16.read(fd, type)
        elif MI_UINT32 == switch:
            val = MI_Uint32.read(fd, type)
        elif MI_SINT32 == switch:
            val = MI_Sint32.read(fd, type)
        elif MI_UINT64 == switch:
            val = MI_Uint64.read(fd, type)
        elif MI_SINT64 == switch:
            val = MI_Sint64.read(fd, type)
        elif MI_REAL32 == switch:
            val = MI_Real32.read(fd, type)
        elif MI_REAL64 == switch:
            val = MI_Real64.read(fd, type)
        elif MI_CHAR16 == switch:
            val = MI_Char16.read(fd, type)
        elif MI_DATETIME == switch:
            val = MI_Datetime.read(fd, type)
        elif MI_STRING == switch:
            val = MI_String.read(fd, type)
        elif MI_INSTANCE == switch:
            val = MI_Instance.read(fd, type)
        elif MI_BOOLEANA == switch:
            val = MI_BooleanA.read(fd, type)
        elif MI_UINT8A == switch:
            val = MI_Uint8A.read(fd, type)
        elif MI_SINT8A == switch:
            val = MI_Sint8A.read(fd, type)
        elif MI_UINT16A == switch:
            val = MI_Uint16A.read(fd, type)
        elif MI_SINT16A == switch:
            val = MI_Sint16A.read(fd, type)
        elif MI_UINT32A == switch:
            val = MI_Uint32A.read(fd, type)
        elif MI_SINT32A == switch:
            val = MI_Sint32A.read(fd, type)
        elif MI_UINT64A == switch:
            val = MI_Uint64A.read(fd, type)
        elif MI_SINT64A == switch:
            val = MI_Sint64A.read(fd, type)
        elif MI_REAL32A == switch:
            val = MI_Real32A.read(fd, type)
        elif MI_REAL64A == switch:
            val = MI_Real64A.read(fd, type)
        elif MI_CHAR16A == switch:
            val = MI_Char16A.read(fd, type)
        elif MI_DATETIMEA == switch:
            val = MI_DatetimeA.read(fd, type)
        elif MI_STRINGA == switch:
            val = MI_StringA.read(fd, type)
        elif MI_INSTANCEA == switch:
            val = MI_InstanceA.read(fd, type)
        else:
            trace('Received unexpected type: ' + str(type))
        verbose_trace('</MI_Value::read>')
        return val


class MI_Boolean(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_BOOLEAN)
        if val is not None:
            # ctypes.c_bool not available if python < 2.6.6
            try:
                self.value = ctypes.c_bool(val)
            except:
                self.value = ctypes.c_int(val)

    def write(self, fd):
        verbose_trace('<MI_Boolean.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            if self.value.value:
                tmp = 1
            else:
                tmp = 0
            buf = struct.pack('@B', tmp)
            fd.sendall(buf)
        verbose_trace('</MI_Boolean.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Boolean.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(1)
            val = struct.unpack('@B', buf)[0]
            verbose_trace('val is ' + str(int(val)) + '\n')
            if val:
                tmp = 'True'
            else:
                tmp = 'False'
            verbose_trace('  value: ' + tmp)
        rval = MI_Boolean(val)
        verbose_trace('</MI_Boolean.read>')
        return rval


class MI_Uint8(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_UINT8)
        if val is not None:
            self.value = ctypes.c_ubyte(val)

    def write(self, fd):
        verbose_trace('<MI_Uint8.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@B', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Uint8.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Uint8.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(1)
            val = struct.unpack('@B', buf)[0]
        rval = MI_Uint8(val)
        verbose_trace('</MI_Uint8.read>')
        return rval


class MI_Sint8(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_SINT8)
        if val is not None:
            self.value = ctypes.c_byte(val)

    def write(self, fd):
        verbose_trace('<MI_Sint8.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@b', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Sint8.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Sint8.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(1)
            val = struct.unpack('@b', buf)[0]
        rval = MI_Sint8(val)
        verbose_trace('</MI_Sint8.read>')
        return rval


class MI_Uint16(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_UINT16)
        if val is not None:
            self.value = ctypes.c_ushort(val)

    def write(self, fd):
        verbose_trace('<MI_Uint16.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@H', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Uint16.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Uint16.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(2)
            val = struct.unpack('@H', buf)[0]
        rval = MI_Uint16(val)
        verbose_trace('</MI_Uint16.read>')
        return rval


class MI_Sint16(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_SINT16)
        if val is not None:
            self.value = ctypes.c_short(val)

    def write(self, fd):
        verbose_trace('<MI_Sint16.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@h', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Sint16.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Sint16.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(2)
            val = struct.unpack('@h', buf)[0]
        rval = MI_Sint16(val)
        verbose_trace('</MI_Sint16.read>')
        return rval


class MI_Uint32(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_UINT32)
        if val is not None:
            self.value = ctypes.c_uint(val)

    def write(self, fd):
        verbose_trace('<MI_Uint32.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@I', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Uint32.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Uint32.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(4)
            val = struct.unpack('@I', buf)[0]
        rval = MI_Uint32(val)
        verbose_trace('</MI_Uint32.read>')
        return rval


class MI_Sint32(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_SINT32)
        if val is not None:
            self.value = ctypes.c_int(val)

    def write(self, fd):
        verbose_trace('<MI_Sint32.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@i', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Sint32.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Sint32.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(4)
            val = struct.unpack('@i', buf)[0]
        rval = MI_Sint32(val)
        verbose_trace('</MI_Sint32.read>')
        return rval


class MI_Uint64(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_UINT64)
        if val is not None:
            self.value = ctypes.c_ulonglong(val)

    def write(self, fd):
        verbose_trace('<MI_Uint64.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@Q', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Uint64.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Uint64.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(8)
            val = struct.unpack('@Q', buf)[0]
        rval = MI_Uint64(val)
        verbose_trace('</MI_Uint64.read>')
        return rval


class MI_Sint64(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_SINT64)
        if val is not None:
            self.value = ctypes.c_longlong(val)

    def write(self, fd):
        verbose_trace('<MI_Sint64.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@q', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Sint64.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Sint64.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(8)
            val = struct.unpack('@q', buf)[0]
        rval = MI_Sint64(val)
        verbose_trace('</MI_Sint64.read>')
        return rval


class MI_Real32(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_REAL32)
        if val is not None:
            self.value = ctypes.c_float(val)

    def write(self, fd):
        verbose_trace('<MI_Real32.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@f', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Real32.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Real32.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(8)
            val = struct.unpack('@f', buf)[0]
        rval = MI_Real32(val)
        verbose_trace('</MI_Real32.read>')
        return rval


class MI_Real64(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_REAL64)
        if val is not None:
            self.value = ctypes.c_double(val)

    def write(self, fd):
        verbose_trace('<MI_Real64.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@d', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Real64.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Real64.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(8)
            val = struct.unpack('@d', buf)[0]
        rval = MI_Real64(val)
        verbose_trace('</MI_Real64.read>')
        return rval


class MI_Char16(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_CHAR16)
        if val is not None:
            self.value = ctypes.c_ushort(val)

    def write(self, fd):
        verbose_trace('<MI_Char16.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            verbose_trace('  value: ' + str(self.value.value))
            buf = struct.pack('@H', self.value.value)
            fd.sendall(buf)
        verbose_trace('</MI_Char16.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Char16.read>')
        val = None
        if 0 == (MI_NULL_FLAG & flags):
            buf = fd.recv(2)
            val = struct.unpack('@H', buf)[0]
        rval = MI_Char16(val)
        verbose_trace('</MI_Char16.read>')
        return rval


class MI_Datetime(MI_Value):
    def __init__(self, isTimestamp):
        MI_Value.__init__(self, MI_DATETIME)
        try:
            self.value = ctypes.c_bool(isTimestamp)
        except:
            self.value = ctypes.c_int(isTimestamp)

    def write(self, fd):
        verbose_trace('  <MI_Datetime.write>')
        MI_Value.write(self, fd)
        verbose_trace('  </MI_Datetime.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Datetime.read>')
        rval = None
        if 0 == (MI_NULL_FLAG & flags):
            rval = MI_Datetime.read_data(fd)
        verbose_trace('</MI_Datetime.read>')
        return rval

    @staticmethod
    def read_data(fd):
        verbose_trace('  <MI_Datetime.read_data>')
        rval = None
        buf = fd.recv(1)
        isTimestamp = None
        try:
            isTimestamp = ctypes.c_bool(struct.unpack('@B', buf)[0])
        except:
            isTimestamp = ctypes.c_int(struct.unpack('@B', buf)[0])
        if isTimestamp:
            rval = MI_Timestamp.read_data(fd)
        else:
            rval = MI_Interval.read_data(fd)
        verbose_trace('  </MI_Datetime.read_data>')
        return rval


class MI_Timestamp(MI_Datetime):
    def __init__(self,
                 year=None,
                 month=None,
                 day=None,
                 hour=None,
                 minute=None,
                 second=None,
                 microseconds=None,
                 utc=None):
        if year is None and \
           month is None and \
           day is None and \
           hour is None and \
           minute is None and \
           second is None and \
           microseconds is None and \
           utc is None:
            MI_Datetime.__init__(self, None)
        else:
            MI_Datetime.__init__(self, True)

        if year is None:
            self.year = ctypes.c_uint(0)
        else:
            self.year = ctypes.c_uint(year)

        if month is None:
            self.month = ctypes.c_uint(0)
        else:
            self.month = ctypes.c_uint(month)

        if day is None:
            self.day = ctypes.c_uint(0)
        else:
            self.day = ctypes.c_uint(day)

        if hour is None:
            self.hour = ctypes.c_uint(0)
        else:
            self.hour = ctypes.c_uint(hour)

        if minute is None:
            self.minute = ctypes.c_uint(0)
        else:
            self.minute = ctypes.c_uint(minute)

        if second is None:
            self.second = ctypes.c_uint(0)
        else:
            self.second = ctypes.c_uint(second)

        if microseconds is None:
            self.microseconds = ctypes.c_uint32(0)
        else:
            self.microseconds = ctypes.c_uint32(microseconds)

        if utc is None:
            self.utc = ctypes.c_int(0)
        else:
            self.utc = ctypes.c_int(int(utc))

    def write(self, fd):
        verbose_trace('<MI_Timestamp.write>')
        MI_Datetime.write(self, fd)
        if self.value is not None:
            self.write_data(fd)
        verbose_trace('</MI_Timestamp.write>')

    def write_data(self, fd):
        verbose_trace('  <MI_Timestamp.write_data>')
        verbose_trace('    isTimestamp:' + str(self.value.value))
        verbose_trace('    year:' + str(self.year.value))
        verbose_trace('    month:' + str(self.month.value))
        verbose_trace('    day:' + str(self.day.value))
        verbose_trace('    hour:' + str(self.hour.value))
        verbose_trace('    minute:' + str(self.minute.value))
        verbose_trace('    second:' + str(self.second.value))
        verbose_trace('    microseconds:' + str(self.microseconds.value))
        verbose_trace('    utc:' + str(self.utc.value))
        buf = struct.pack('@B', self.value.value)
        buf += struct.pack('@I', self.year.value)
        buf += struct.pack('@I', self.month.value)
        buf += struct.pack('@I', self.day.value)
        buf += struct.pack('@I', self.hour.value)
        buf += struct.pack('@I', self.minute.value)
        buf += struct.pack('@I', self.second.value)
        buf += struct.pack('@I', self.microseconds.value)
        buf += struct.pack('@i', self.utc.value)
        fd.sendall(buf)
        verbose_trace('  </MI_Timestamp.write_data>')

    @staticmethod
    def read_data(fd):
        verbose_trace('    <MI_Timestamp.read_data>')
        buf = fd.recv(4)
        year = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        month = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        day = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        hour = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        minute = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        second = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        microseconds = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        utc = struct.unpack('@i', buf)[0]
        buf = fd.recv(4)
        rval = MI_Timestamp(year, month, day, hour, minute, second,
                            microseconds, utc)
        verbose_trace('      isTimestamp: True')
        verbose_trace('      year:' + str(year))
        verbose_trace('      month:' + str(month))
        verbose_trace('      day:' + str(day))
        verbose_trace('      hour:' + str(hour))
        verbose_trace('      minute:' + str(minute))
        verbose_trace('      second:' + str(second))
        verbose_trace('      microseconds:' + str(microseconds))
        verbose_trace('      utc:' + str(utc))
        verbose_trace('    </MI_Timestamp.read_data>')
        return rval

    @staticmethod
    def from_time(seconds):
        verbose_trace('<MI_Timestamp.from_time>')
        tm = time.gmtime(seconds)
        ts = MI_Timestamp(tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour,
                           tm.tm_min, tm.tm_sec, 0, time.timezone / 60)
        verbose_trace('</MI_Timestamp.from_time>')
        return ts


class MI_Interval(MI_Datetime):
    def __init__(self,
                 days=None,
                 hours=None,
                 minutes=None,
                 seconds=None,
                 microseconds=None):
        if days is None and \
           hours is None and \
           minutes is None and \
           seconds is None and \
           microseconds is None:
            MI_Datetime.__init__(self, None)
        else:
            MI_Datetime.__init__(self, True)

        if days is None:
            self.day = ctypes.c_uint(0)
        else:
            self.days = ctypes.c_uint(days)

        if hours is None:
            self.hour = ctypes.c_uint(0)
        else:
            self.hours = ctypes.c_uint(hours)

        if minutes is None:
            self.minute = ctypes.c_uint(0)
        else:
            self.minutes = ctypes.c_uint(minutes)

        if seconds is None:
            self.second = ctypes.c_uint(0)
        else:
            self.seconds = ctypes.c_uint(seconds)

        if microseconds is None:
            self.microseconds = ctypes.c_uint32(0)
        else:
            self.microseconds = ctypes.c_uint32(microseconds)

    def write(self, fd):
        verbose_trace('<MI_Interval.write>')
        MI_Datetime.write(self, fd)
        if self.value is not None:
            self.write_data(fd)
        verbose_trace('</MI_Interval.write>')

    def write_data(self, fd):
        verbose_trace('  <MI_Interval.write_data>')
        verbose_trace('    isTimestamp:' + str(self.value.value))
        verbose_trace('    days:' + str(self.days.value))
        verbose_trace('    hours:' + str(self.hours.value))
        verbose_trace('    minutes:' + str(self.minutes.value))
        verbose_trace('    seconds:' + str(self.seconds.value))
        verbose_trace('    microseconds:' + str(self.microseconds.value))
        buf = struct.pack('@B', self.value.value)
        buf += struct.pack('@I', self.days.value)
        buf += struct.pack('@I', self.hours.value)
        buf += struct.pack('@I', self.minutes.value)
        buf += struct.pack('@I', self.seconds.value)
        buf += struct.pack('@I', self.microseconds.value)
        fd.sendall(buf)
        verbose_trace('  </MI_Interval.write>')

    @staticmethod
    def read_data(fd):
        verbose_trace('    <MI_Interval.read_data>')
        buf = fd.recv(4)
        days = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        hours = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        minutes = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        seconds = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        microseconds = struct.unpack('@I', buf)[0]
        buf = fd.recv(4)
        rval = MI_Timestamp(days, hours, minutes, seconds, microseconds)
        verbose_trace('      isTimestamp: False')
        verbose_trace('      days:' + str(days))
        verbose_trace('      hours:' + str(hours))
        verbose_trace('      minutes:' + str(minutes))
        verbose_trace('      seconds:' + str(seconds))
        verbose_trace('      microseconds:' + str(microseconds))
        verbose_trace('    </MI_Interval.read_data>')
        return rval


class MI_String(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_STRING)
        if val is not None:
            self.value = val

    def write(self, fd):
        verbose_trace('<MI_String.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            buf = struct.pack('@i', len(self.value))
            if type(buf) is not str: # python3
                if type(self.value) == str:
                    buf = struct.pack('@i', len(bytes(self.value,'utf8')))
                    buf += bytes(self.value, 'utf8')
                    verbose_trace('  len: ' + str(len(self.value)) + ', value: ' +
                                  self.value)
                else:
                    buf += self.value
                    verbose_trace('  len: ' + str(len(self.value)) + ', value: ' +
                                  self.value.decode('utf8'))
            else: # python 2
                if type(self.value) != str: # unicode
                    buf = struct.pack('@i', len(self.value.encode('utf8')))
                    buf+=self.value.encode('utf8')
                else:
                    buf += self.value
                verbose_trace('  len: ' + str(len(self.value)) + ', value: ' +
                                  self.value)

            fd.sendall(buf)
        verbose_trace('</MI_String.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_String.read>')
        strg = None
        if 0 == (MI_NULL_FLAG & flags):
            strg = read_string(fd)
        rval = MI_String(strg)
        verbose_trace('</MI_String.read>')
        return rval


class MI_Instance(MI_Value):
    def __init__(self, val):
        MI_Value.__init__(self, MI_INSTANCE)
        if val is not None:
            self.value = dict(val)

    def write(self, fd):
        verbose_trace('<MI_Instance.write>')
        MI_Value.write(self, fd)
        if self.value is not None:
            write_values(fd, self.value)
        verbose_trace('</MI_Instance.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Instance.read>')
        instance = None
        if 0 == (MI_NULL_FLAG & flags):
            instance = read_values(fd)
        rval = MI_Instance(instance)
        verbose_trace('</MI_Instance.read>')
        return rval


class MI_BooleanA(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_BOOLEANA)
        self.value = []
        if vals is not None:
            for val in vals:
                try:
                    self.value.append(ctypes.c_bool(val))
                except:
                    self.value.append(ctypes.c_int(val))

    def write(self, fd):
        verbose_trace('<MI_BooleanA.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                if val.value:
                    tmp = 1
                else:
                    tmp = 0
                buf = struct.pack('@B', tmp)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_BooleanA.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_BooleanA.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(1)
                val = struct.unpack('@B', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_BooleanA(vals)
        verbose_trace('</MI_BooleanA.read>')
        return rval


class MI_Uint8A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_UINT8A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_ubyte(val))

    def write(self, fd):
        verbose_trace('<MI_Uint8A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@B', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Uint8A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_UintA.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(1)
                val = struct.unpack('@B', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Uint8A(vals)
        verbose_trace('</MI_Uint8A.read>')
        return rval


class MI_Sint8A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_SINT8A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_byte(val))

    def write(self, fd):
        verbose_trace('<MI_Sint8A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@b', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Sint8A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_SintA.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(1)
                val = struct.unpack('@b', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Sint8A(vals)
        verbose_trace('</MI_Sint8A.read>')
        return rval


class MI_Uint16A(MI_Value):
    def __init__(self, vals):
        verbose_trace('<MI_Uint16A>')
        MI_Value.__init__(self, MI_UINT16A)
        self.value = []
        if vals is not None:
            for val in vals:
                if isinstance(val, ctypes.c_ushort):
                    self.value.append(val)
                else:
                    self.value.append(ctypes.c_ushort(val))
        verbose_trace('</MI_Uint16A>')

    def write(self, fd):
        verbose_trace('<MI_Uint16A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('  <values>')
            for val in self.value:
                verbose_trace('    value: ' + str(val.value))
                buf = struct.pack('@H', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Uint16A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Uint16A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(2)
                val = struct.unpack('@H', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Uint16A(vals)
        verbose_trace('</MI_Uint16A.read>')
        return rval


class MI_Sint16A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_SINT16A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_short(val))

    def write(self, fd):
        verbose_trace('<MI_Sint16A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@h', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Sint16A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Sint16A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            len = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(len))
            for _ in range(len):
                buf = fd.recv(2)
                val = struct.unpack('@h', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Sint16A(vals)
        verbose_trace('</MI_Sint16A.read>')
        return rval


class MI_Uint32A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_UINT32A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_uint(val))

    def write(self, fd):
        verbose_trace('<MI_Uint32A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@I', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Uint32A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Uint32A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(4)
                val = struct.unpack('@I', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Uint32A(vals)
        verbose_trace('</MI_Uint32A.read>')
        return rval


class MI_Sint32A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_SINT32A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_int(val))

    def write(self, fd):
        verbose_trace('<MI_Sint32A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('    len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@i', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Sint32A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Sint32A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(4)
                val = struct.unpack('@i', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Sint32A(vals)
        verbose_trace('</MI_Sint32A.read>')
        return rval


class MI_Uint64A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_UINT64A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_ulonglong(val))

    def write(self, fd):
        verbose_trace('<MI_Uint64A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@Q', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Uint64A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Uint64A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(8)
                val = struct.unpack('@Q', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Uint64A(vals)
        verbose_trace('</MI_Uint64A.read>')
        return rval


class MI_Sint64A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_SINT64A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_longlong(val))

    def write(self, fd):
        verbose_trace('<MI_Sint64A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@q', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Sint64A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Sint64A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            len = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(len))
            for _ in range(len):
                buf = fd.recv(8)
                val = struct.unpack('@q', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Sint64A(vals)
        verbose_trace('</MI_Sint64A.read>')
        return rval


class MI_Real32A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_REAL32A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_float(val))

    def write(self, fd):
        verbose_trace('<MI_Real32A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@f', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Real32A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Real32A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(4)
                val = struct.unpack('@f', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Real32A(vals)
        verbose_trace('</MI_Real32A.read>')
        return rval


class MI_Real64A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_REAL64A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_double(val))

    def write(self, fd):
        verbose_trace('<MI_Real64A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('    <values>')
            for val in self.value:
                verbose_trace('      value: ' + str(val.value))
                buf = struct.pack('@d', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Real64A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Real64A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                buf = fd.recv(8)
                val = struct.unpack('@d', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Real64A(vals)
        verbose_trace('</MI_Real64A.read>')
        return rval


class MI_Char16A(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_CHAR16A)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(ctypes.c_ushort(val))

    def write(self, fd):
        verbose_trace('<MI_Char16A.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('  <values>')
            for val in self.value:
                verbose_trace('    value: ' + str(val.value))
                buf = struct.pack('@H', val.value)
                fd.sendall(buf)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_Char16A.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_Char16A.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for i in range(length):
                buf = fd.recv(2)
                val = struct.unpack('@H', buf)[0]
                verbose_trace('  value: ' + str(val))
                vals.append(val)
        rval = MI_Char16A(vals)
        verbose_trace('</MI_Char16A.read>')
        return rval


class MI_DatetimeA(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_DATETIMEA)
        self.values = []
        for val in vals:
            self.values.append(val)

    def write(self, fd):
        verbose_trace('<MI_DatetimeA.write>')
        MI_Value.write(self, fd)
        verbose_trace('  len:' + str(len(self.values)))
        buf = struct.pack('@i', len(self.values))
        fd.sendall(buf)
        for val in self.values:
            val.write_data(fd)
        verbose_trace('</MI_DatetimeA.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_DatetimeA.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                val = MI_Datetime.read_data(fd)
                vals.append(val)
        rval = MI_DatetimeA(vals)
        verbose_trace('</MI_DatetimeA.read>')
        return rval


class MI_StringA(MI_Value):
    def __init__(self, vals):
        MI_Value.__init__(self, MI_STRINGA)
        self.value = []
        if vals is not None and len(vals) > 0:
            for val in vals:
                self.value.append(val)

    def write(self, fd):
        verbose_trace('<MI_StringA.write>')
        if self.value is not None and 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('  <values>')
            for val in self.value:
                write_string(fd, val)
            verbose_trace('  </values>')
        else:
            verbose_trace('    type: ' + str(self.type))
            fd.sendall(struct.pack('@B', self.type | MI_NULL_FLAG))
        verbose_trace('</MI_StringA.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_StringA.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for i in range(length):
                strg = read_string(fd)
                vals.append(strg)
        rval = MI_StringA(vals)
        verbose_trace('</MI_StringA.read>')
        return rval


class MI_InstanceA(MI_Value):
    def __init__(self, vals=None):
        MI_Value.__init__(self, MI_INSTANCEA)
        self.value = []
        if vals is not None:
            for val in vals:
                self.value.append(val)

    def write(self, fd):
        verbose_trace('<MI_InstanceA.write>')
        if 0 < len(self.value):
            MI_Value.write(self, fd)
            verbose_trace('  len:' + str(len(self.value)))
            buf = struct.pack('@i', len(self.value))
            fd.sendall(buf)
            verbose_trace('  <values>')
            for val in self.value:
                write_values(fd, val)
            verbose_trace('  </values>')
        else:
            verbose_trace('    len: 0')
            verbose_trace('    type: ' + str(self.type | MI_NULL_FLAG))
            buf = struct.pack('@B', self.type | MI_NULL_FLAG)
            fd.sendall(buf)
        verbose_trace('</MI_InstanceA.write>')

    @staticmethod
    def read(fd, flags):
        verbose_trace('<MI_InstanceA.read>')
        vals = None
        if 0 == (MI_NULL_FLAG & flags):
            vals = []
            buf = fd.recv(4)
            length = struct.unpack('@i', buf)[0]
            verbose_trace('  len:' + str(length))
            for _ in range(length):
                val = read_values(fd)
                sys.stderr.write('....' + repr(val) + '....\n')
                vals.append(val)
        rval = MI_InstanceA(vals)
        verbose_trace('</MI_InstanceA.read>')
        return rval

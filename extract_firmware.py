#!/usr/bin/python
#
# Copyright 2013 Ilia Mirkin.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

import itertools
import mmap
import os
import re
import struct
import sys
import tempfile
import urllib
import zlib

# The firmware changes fairly rarely. From a limited sample, when the
# firmware does change, the starts of the firmware remain the
# same. When changing the version though, one should double-check the
# sizes, which can be different.
#
# This is the list of tested versions that produce the same binaries
VERSIONS = (
    "319.17",
    "319.23",
    "319.32",
    "325.08",
    "325.15",
    "340.32",
    )

ARCHES = ("x86_64", "x86")

def product(a, b):
    for x in a:
        for y in b:
            yield (x, y)

cwd = os.getcwd()
for (VERSION, ARCH) in product(VERSIONS, ARCHES):
    if os.path.exists("NVIDIA-Linux-%s-%s" % (ARCH, VERSION)):
        break
else:
    print """Please run this in a directory where NVIDIA-Linux-x86-%(version)s is a subdir.

You can make this happen by running
wget http://us.download.nvidia.com/XFree86/Linux-x86/%(version)s/NVIDIA-Linux-x86-%(version)s.run
sh NVIDIA-Linux-x86-%(version)s.run --extract-only

Note: You can use other versions/arches, see the source for what is acceptable.
""" % {"version": VERSIONS[-1]}
    sys.exit(1)

kernel_f = open("NVIDIA-Linux-%s-%s/kernel/nv-kernel.o" % (ARCH, VERSION), "r")
kernel = mmap.mmap(kernel_f.fileno(), 0, access=mmap.ACCESS_READ)

user_f = open("NVIDIA-Linux-%s-%s/libnvcuvid.so.%s" % (ARCH, VERSION, VERSION), "r")
user = mmap.mmap(user_f.fileno(), 0, access=mmap.ACCESS_READ)

vp2_kernel_prefix = "\xcd\xab\x55\xee\x44"
vp2_user_prefix = "\xce\xab\x55\xee\x20\x00\x00\xd0\x00\x00\x00\xd0"
vp4_kernel_prefix = "\xf1\x97\x00\x42\xcf\x99"
vp3_user_prefix = "\x64\x00\xf0\x20\x64\x00\xf1\x20\x64\x00\xf2\x20"
vp3_vc1_prefix = "\x43\x00\x00\x34" * 2

# List of chip revisions since the fuc loader expects nvXX_fucXXX files
VP2_CHIPS = ["nv84"] # there are more, but no need for more symlinks
VP3_CHIPS = ["nv98", "nvaa", "nvac"]
VP4_0_CHIPS = ["nva3", "nva5", "nva8", "nvaf"] # nvaf is 4.1, but same fw
VP4_2_CHIPS = ["nvc0", "nvc1", "nvc3", "nvc4", "nvc8", "nvce", "nvcf"]
VP5_CHIPS = ["nvd7", "nvd9", "nve4", "nve6", "nve7", "nvf0", "nvf1", "nv106", "nv108"]

def links(chips, tail):
    return list("%s_%s" % (chip, tail) for chip in chips)

def vp3_offset():
    # Note: 340 uses higher offset, 325 uses lower. Guessing 330 as the cutoff.
    if float(VERSION) < 330:
        return 2287
    return 2286

def vp5_offset():
    # Note: 340 uses higher offset, 325 uses lower. Guessing 330 as the cutoff.
    if float(VERSION) < 330:
        return 0xb3
    return 0xb7

BLOBS = {
    # VP2 kernel xuc
    "nv84_bsp": {
        "data": kernel,
        "start": vp2_kernel_prefix + "\x46",
        "length": 0x16f3c,
        "links": links(VP2_CHIPS, "xuc103"),
    },
    "nv84_vp": {
        "data": kernel,
        "start": vp2_kernel_prefix + "\x7c",
        "length": 0x1ae6c,
        "links": links(VP2_CHIPS, "xuc00f"),
    },

    # VP3 kernel fuc
    "nv98_bsp": {
        "data": kernel,
        "start": "\xf1\x07\x00\x10\xf1\x03\x00\x00",
        "length": 0xac00,
        "pred": lambda data, i: data[i+vp3_offset()] == '\x8e',
        "links": links(VP3_CHIPS, "fuc084"),
    },
    "nv98_vp": {
        "data": kernel,
        "start": "\xf1\x07\x00\x10\xf1\x03\x00\x00",
        "length": 0xa500,
        "pred": lambda data, i: data[i+vp3_offset()] == '\x95',
        "links": links(VP3_CHIPS, "fuc085"),
    },
    "nv98_ppp": {
        "data": kernel,
        "start": "\xf1\x07\x00\x08\xf1\x03\x00\x00",
        "length": 0x3800,
        "pred": lambda data, i: data[i+vp3_offset()] == '\x30',
        "links": links(VP3_CHIPS, "fuc086"),
    },

    # VP4.0 kernel fuc
    "nva3_bsp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0x10200,
        "pred": lambda data, i: data[i+8*11+1] == '\xcf',
        "links": links(VP4_0_CHIPS, "fuc084"),
    },
    "nva3_vp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0xc600,
        "pred": lambda data, i: data[i+8*11+1] == '\x9e',
        "links": links(VP4_0_CHIPS, "fuc085"),
    },
    "nva3_ppp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0x3f00,
        "pred": lambda data, i: data[i+8*11+1] == '\x36',
        "links": links(VP4_0_CHIPS, "fuc086"),
    },

    # VP4.2 kernel fuc
    "nvc0_bsp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0x10d00,
        "pred": lambda data, i: data[i+0x59] == '\xd8',
        "links": links(VP4_2_CHIPS, "fuc084"),
    },
    "nvc0_vp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0xd300,
        "pred": lambda data, i: data[i+0x59] == '\xa5',
        "links": links(VP4_2_CHIPS, "fuc085"),
    },
    "nvc0_ppp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0x4100,
        "pred": lambda data, i: data[i+0x59] == '\x38',
        "links": links(VP4_2_CHIPS, "fuc086") + links(VP5_CHIPS, "fuc086"),
    },

    # VP5 kernel fuc
    "nve0_bsp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0x11c00,
        "pred": lambda data, i: data[i+vp5_offset()] == '\x27',
        "links": links(VP5_CHIPS, "fuc084"),
    },
    "nve0_vp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0xdd00,
        "pred": lambda data, i: data[i+vp5_offset()] == '\x0a',
        "links": links(VP5_CHIPS, "fuc085"),
    },

    # VP2 user xuc
    "nv84_bsp-h264": {
        "data": user,
        "start": vp2_user_prefix + "\x88",
        "length": 0xd9d0,
    },
    "nv84_vp-h264-1": {
        "data": user,
        "start": vp2_user_prefix + "\x3c",
        "length": 0x1f334,
    },
    "nv84_vp-h264-2": {
        "data": user,
        "start": vp2_user_prefix + "\x04",
        "length": 0x1bffc,
    },
    "nv84_vp-mpeg12": {
        "data": user,
        "start": vp2_user_prefix + "\x4c",
        "length": 0x22084,
    },
    "nv84_vp-vc1-1": {
        "data": user,
        "start": vp2_user_prefix + "\x7c",
        "length": 0x2cd24,
    },
    "nv84_vp-vc1-2": {
        "data": user,
        "start": vp2_user_prefix + "\xa4",
        "length": 0x1535c,
    },
    "nv84_vp-vc1-3": {
        "data": user,
        "start": vp2_user_prefix + "\x34",
        "length": 0x133bc,
    },

    # VP3 user vuc
    "vuc-vp3-mpeg12-0": {
        "data": user,
        "start": vp3_user_prefix,
        "length": 0xb00,
        "pred": lambda data, i: data[i + 11 * 8] == '\x4a' and data[i + 228] == '\x43',
    },
    "vuc-vp3-h264-0": {
        "data": user,
        "start": vp3_user_prefix,
        "length": 0x1600,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\xff' and data[i + 225] == '\x81',
    },
    "vuc-vp3-vc1-0": {
        "data": user,
        "start": vp3_vc1_prefix + vp3_user_prefix,
        "length": 0x1d00,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\xf4',
    },
    "vuc-vp3-vc1-1": {
        "data": user,
        "start": vp3_vc1_prefix + vp3_user_prefix,
        "length": 0x2100,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\x34',
    },
    "vuc-vp3-vc1-2": {
        "data": user,
        "start": vp3_vc1_prefix + vp3_user_prefix,
        "length": 0x2300,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\x98',
    },

    # VP4.x user vuc
    "vuc-vp4-mpeg12-0": {
        "data": user,
        "start": vp3_user_prefix,
        "length": 0xc00,
        "pred": lambda data, i: data[i + 11 * 8] == '\x4a' and data[i + 228] == '\x44',
        "links": ["vuc-mpeg12-0"],
    },
    "vuc-vp4-h264-0": {
        "data": user,
        "start": vp3_user_prefix,
        "length": 0x1900,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\xff' and data[i + 225] == '\x8c',
        "links": ["vuc-h264-0"],
    },
    "vuc-vp4-mpeg4-0": {
        "data": user,
        "start": vp3_user_prefix,
        "length": 0x1d00,
        "pred": lambda data, i: data[i + 61] == '\x30' and data[i + 6923] == '\x00',
        "links": ["vuc-mpeg4-0"],
    },
    "vuc-vp4-mpeg4-1": {
        "data": user,
        "start": vp3_user_prefix,
        "length": 0x1d00,
        "pred": lambda data, i: data[i + 61] == '\x30' and data[i + 6923] == '\x20',
        "links": ["vuc-mpeg4-1"],
    },
    "vuc-vp4-vc1-0": {
        "data": user,
        "start": vp3_vc1_prefix + vp3_user_prefix,
        "length": 0x1d00,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\xb4',
        "links": ["vuc-vc1-0"],
    },
    "vuc-vp4-vc1-1": {
        "data": user,
        "start": vp3_vc1_prefix + vp3_user_prefix,
        "length": 0x2100,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\x08',
        "links": ["vuc-vc1-1"],
    },
    "vuc-vp4-vc1-2": {
        "data": user,
        "start": vp3_vc1_prefix + vp3_user_prefix,
        "length": 0x2100,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\x6c',
        "links": ["vuc-vc1-2"],
    },
}

# Build a regex on the start data to speed things along.
start_re = "|".join(set(re.escape(v["start"]) for v in BLOBS.itervalues()))
files = set(v["data"] for v in BLOBS.itervalues())

done = set()

for data in files:
    for match in re.finditer(start_re, data):
        for name, v in BLOBS.iteritems():
            if name in done or data != v["data"] or match.group(0) != v["start"]:
                continue

            i = match.start(0)
            pred = v.get("pred")
            if pred and not pred(data, i):
                continue

            length = v["length"]
            links = v.get("links", [])

            with open(os.path.join(cwd, name), "w") as f:
                f.write(data[i:i+length])

            done.add(name)
            for link in links:
                try:
                    os.unlink(link)
                except:
                    pass
                os.symlink(name, link)

for name in set(BLOBS) - done:
    print "Firmware %s not found, ignoring." % name

ARCHIVE_ORDERS = {
    "325.15": ["nvc0", "nvc8", "nvc3", "nvc4", "nvce", "nvcf", "nvc1",
               "nvd7", "nvd9", "nve4", "nve7", "nve6", "nvf0", "nvf1",
               "nv108"],
}

# Extract the gzipped archives found inside the kernel driver
def decompress(prefix, start, s):
    try:
        decomp = zlib.decompressobj(-zlib.MAX_WBITS)
        data = decomp.decompress(s[10:])
    except Exception, e:
        print prefix, repr(s[:16]), len(s)
        print e
        return False
    magic, count = struct.unpack("<II", data[:8])
    if magic != 0:
        print "Skipping gzip blob at 0x%x (%d bytes)" % (start, len(data))
        return False

    # Allow valid archives to be skipped
    if not prefix:
        return True

    entries = []
    # Each entry is id, length, offset
    for i in xrange(count):
        entry = struct.unpack("<III", data[8 + i * 12:8 + (i + 1) * 12])
        entries.append(entry)

    return True

if VERSION in ARCHIVE_ORDERS:
    gzip_starts = list(m.start(0) for m in re.finditer(
        re.escape("\x1f\x8b\x08"), kernel))
    idx = 0
    names = ARCHIVE_ORDERS[VERSION]
    for start, end in itertools.izip(gzip_starts,
                                     gzip_starts[1:] + [len(kernel)]):
        if decompress(names[idx], start, kernel[start:end]):
            idx += 1
    if idx != len(names):
        print "Unexpected quantity of archives in blob, graph fw likely wrong."
else:
    print "Unknown PGRAPH archive order in this version."

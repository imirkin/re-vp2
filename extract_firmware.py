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

import mmap
import os
import re
import sys
import tempfile
import urllib

# From a limited sample, while the (user) firmware does change from
# version to version, the starts of the firmware remain the same. When
# changing the version though, one should look at an mmt trace and see
# how much data the m2mf channel copies for each thing. The changes
# are usually on the order of 10s of bytes between versions.
VERSION = "319.23"

cwd = os.getcwd()
if not os.path.exists("NVIDIA-Linux-x86_64-%s" % VERSION):
    print """Please run this in a directory where NVIDIA-Linux-x86_64-%(version)s is a subdir.

You can make this happen by running
wget http://us.download.nvidia.com/XFree86/Linux-x86_64/%(version)s/NVIDIA-Linux-x86_64-%(version)s.run
sh NVIDIA-Linux-x86_64-%(version)s.run --extract-only
""" % {"version": VERSION}
    sys.exit(1)

kernel_f = open("NVIDIA-Linux-x86_64-%s/kernel/nv-kernel.o" % VERSION, "r")
kernel = mmap.mmap(kernel_f.fileno(), 0, access=mmap.ACCESS_READ)

user_f = open("NVIDIA-Linux-x86_64-%s/libnvcuvid.so.%s" % (VERSION, VERSION), "r")
user = mmap.mmap(user_f.fileno(), 0, access=mmap.ACCESS_READ)

vp2_kernel_prefix = "\xcd\xab\x55\xee\x44"
vp2_user_prefix = "\xce\xab\x55\xee\x20\x00\x00\xd0\x00\x00\x00\xd0"
vp4_kernel_prefix = "\xf1\x97\x00\x42\xcf\x99"
vp4_user_prefix = "\x64\x00\xf0\x20\x64\x00\xf1\x20\x64\x00\xf2\x20"
vp4_vc1_prefix = "\x43\x00\x00\x34" * 2

# List of chip revisions since the fuc loader expects nvXX_fucXXX files
VP2_CHIPS = ["nv84"] # there are more, but no need for more symlinks
VP3_CHIPS = ["nv98", "nvaa", "nvac"]
VP4_0_CHIPS = ["nva3", "nva5", "nva8", "nvaf"] # nvaf is 4.1, but same fw
VP4_2_CHIPS = ["nvc0", "nvc1", "nvc3", "nvc4", "nvc8", "nvce", "nvcf"]
VP5_CHIPS = ["nvd7", "nvd9", "nve4", "nve6", "nve7", "nvf0", "nv108"]

def links(chips, tail):
    return list("%s_%s" % (chip, tail) for chip in chips)

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
        "pred": lambda data, i: data[i+0xb3] == '\x27',
        "links": links(VP5_CHIPS, "fuc084"),
    },
    "nve0_vp": {
        "data": kernel,
        "start": vp4_kernel_prefix,
        "length": 0xdd00,
        "pred": lambda data, i: data[i+0xb3] == '\x0a',
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

    # VP4.x user vuc
    "vuc-mpeg12-0": {
        "data": user,
        "start": vp4_user_prefix,
        "length": 0xc00,
        "pred": lambda data, i: data[i + 11 * 8] == '\x4a' and data[i + 228] == '\x44',
    },
    "vuc-h264-0": {
        "data": user,
        "start": vp4_user_prefix,
        "length": 0x1900,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\xff' and data[i + 225] == '\x8c',
    },
    "vuc-mpeg4-0": {
        "data": user,
        "start": vp4_user_prefix,
        "length": 0x1d00,
        "pred": lambda data, i: data[i + 61] == '\x30' and data[i + 6923] == '\x00',
    },
    "vuc-mpeg4-1": {
        "data": user,
        "start": vp4_user_prefix,
        "length": 0x1d00,
        "pred": lambda data, i: data[i + 61] == '\x30' and data[i + 6923] == '\x20',
    },
    "vuc-vc1-0": {
        "data": user,
        "start": vp4_vc1_prefix + vp4_user_prefix,
        "length": 0x1d00,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\xb4',
    },
    "vuc-vc1-1": {
        "data": user,
        "start": vp4_vc1_prefix + vp4_user_prefix,
        "length": 0x2100,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\x08',
    },
    "vuc-vc1-2": {
        "data": user,
        "start": vp4_vc1_prefix + vp4_user_prefix,
        "length": 0x2100,
        "pred": lambda data, i: data[i + 11 * 8 + 1] == '\x6c',
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

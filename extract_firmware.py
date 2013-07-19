#!/usr/bin/python

import mmap
import os
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
# dirname = tempfile.mkdtemp()
# urllib.urlretrieve("http://us.download.nvidia.com/XFree86/Linux-x86_64/%s/NVIDIA-Linux-x86_64-%s.run" % (VERSION, VERSION), os.path.join(dirname, "nv.run"))
# os.chdir(dirname)
# os.system("sh nv.run --extract-only")
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

vp2_user_prefix = "\xce\xab\x55\xee\x20\x00\x00\xd0\x00\x00\x00\xd0"
vp4_user_prefix = "\x64\x00\xf0\x20\x64\x00\xf1\x20\x64\x00\xf2\x20"
vp4_vc1_prefix = "\x43\x00\x00\x34" * 2

VP2_CHIPS = ["nv84"]
VP4_CHIPS = ["nva3"]

def links(chips, tail):
    return list("%s_%s" % (chip, tail) for chip in chips)

BLOBS = {
    "nv84_bsp": {
        "data": kernel,
        "start": "\xcd\xab\x55\xee\x44\x46",
        "length": 0x16f3c,
        "links": links(VP2_CHIPS, "xuc103"),
    },
    "nv84_vp": {
        "data": kernel,
        "start": "\xcd\xab\x55\xee\x44\x7c",
        "length": 0x1ae6c,
        "links": links(VP2_CHIPS, "xuc00f"),
    },

    "nva3_bsp": {
        "data": kernel,
        "start": "\xf1\x97\x00\x42\xcf\x99",
        "length": 0x10200,
        "pred": lambda data, i: data[i+8*11+1] == '\xcf',
        "links": links(VP4_CHIPS, "fuc084"),
    },
    "nva3_vp": {
        "data": kernel,
        "start": "\xf1\x97\x00\x42\xcf\x99",
        "length": 0xc600,
        "pred": lambda data, i: data[i+8*11+1] == '\x9e',
        "links": links(VP4_CHIPS, "fuc085"),
    },
    "nva3_ppp": {
        "data": kernel,
        "start": "\xf1\x97\x00\x42\xcf\x99",
        "length": 0x3f00,
        "pred": lambda data, i: data[i+8*11+1] == '\x36',
        "links": links(VP4_CHIPS, "fuc086"),
    },

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

# Looks like they have logic to patch up certain blobs. Very
# weird. Perhaps it's done differently for different chipsets, but I
# don't currently have enough data to determine that.
PATCHES = [
    {
        "data": kernel,
        "start": "\xf7\x92\xef\xce\x9e\x49\x26\xce",
        "length": 48 + 96 + 16 * 5,
        "file": "nva3_bsp",
        "patches": [
            {"offset": 1104, "length": 48},
            {"offset": 1168, "length": 96},
            {"offset": 55552, "length": 16},
            {"offset": 58464, "length": 16},
            {"offset": 61216, "length": 16},
            {"offset": 64816, "length": 16},
            {"offset": 65280, "length": 16},
        ],
    },
]

for name, v in BLOBS.iteritems():
    data = v["data"]
    start = v["start"]
    length = v["length"]
    pred = v.get("pred")
    links = v.get("links")
    for i in xrange(data.size()):
        if data[i:i+len(start)] == start:
            if pred and not pred(data, i):
                continue
            with open(os.path.join(cwd, name), "w") as f:
                f.write(data[i:i+length])
            break
    else:
        print "Firmware %s not found, ignoring." % name
        continue

    if links:
        for link in links:
            try:
                os.unlink(link)
            except:
                pass
            os.symlink(name, link)

for v in PATCHES:
    data = v["data"]
    start = v["start"]
    length = v["length"]
    fname = v["file"]
    for i in xrange(data.size()):
        if data[i:i+len(start)] == start:
            with open(os.path.join(cwd, fname), "r+") as f:
                for p in v["patches"]:
                    f.seek(p["offset"])
                    f.write(data[i:i+p["length"]])
                    i += p["length"]
            break
    else:
        print "Unable to find patch for %s, ignoring." % fname

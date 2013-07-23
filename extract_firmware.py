#!/usr/bin/python

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
    # It has been suggested that these patches don't really
    # matter. Ignore them for now.
    #
    # {
    #     "data": kernel,
    #     "start": "\xf7\x92\xef\xce\x9e\x49\x26\xce",
    #     "length": 48 + 96 + 16 * 5,
    #     "file": "nva3_bsp",
    #     "patches": [
    #         {"offset": 1104, "length": 48},
    #         {"offset": 1168, "length": 96},
    #         {"offset": 55552, "length": 16},
    #         {"offset": 58464, "length": 16},
    #         {"offset": 61216, "length": 16},
    #         {"offset": 64816, "length": 16},
    #         {"offset": 65280, "length": 16},
    #     ],
    # },
    #
    # nvc0_bsp, nve0_bsp also need a patch. perhaps nvc0_vp as well.
]

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

# TODO: When there are multiple patches, switch this to the regex method
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

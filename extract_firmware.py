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

user_prefix = "\xce\xab\x55\xee\x20\x00\x00\xd0\x00\x00\x00\xd0"

BLOBS = {
    "bsp": {
        "data": kernel,
        "start": "\xcd\xab\x55\xee\x44\x46",
        "length": 0x16f3c,
    },
    "vp": {
        "data": kernel,
        "start": "\xcd\xab\x55\xee\x44\x7c",
        "length": 0x1ae6c,
    },

    "bsp-h264": {
        "data": user,
        "start": user_prefix + "\x88",
        "length": 0xd9d0,
    },
    "vp-h264-1": {
        "data": user,
        "start": user_prefix + "\x3c",
        "length": 0x1f334,
    },
    "vp-h264-2": {
        "data": user,
        "start": user_prefix + "\x04",
        "length": 0x1bffc,
    },
}

for name, v in BLOBS.iteritems():
    data = v["data"]
    start = v["start"]
    length = v["length"]
    for i in xrange(data.size()):
        if data[i:i+len(start)] == start:
            with open(os.path.join(cwd, "nv84_" + name), "w") as f:
                f.write(data[i:i+length])
            break
    else:
        raise RuntimeException("Firmware %s not found." % name)

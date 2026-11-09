#!/usr/bin/env python3
# 用 select() 验证 /dev/pir 的 poll 路径
import os
import select
import time

fd = os.open("/dev/pir", os.O_RDONLY)
t0 = time.time()
events = 0
while time.time() - t0 < 70:
    r, _, _ = select.select([fd], [], [], 5.0)
    if r:
        data = os.read(fd, 8)
        events += 1
        print("event %d at %.1fs data=%r" % (events, time.time() - t0, data))
        if events >= 3:
            break
    else:
        print("select timeout (5s), no event")
os.close(fd)
print("total events=%d" % events)

import sys
import time
import datetime
import numpy as np
import os
import fcntl

fcntl.fcntl(sys.stdin,fcntl.F_SETFL,os.O_NONBLOCK)

year,mon,day = time.gmtime(time.time())[0:3]

lines = np.array([])
linetimes = np.array([])
lines1sec = np.array([])
tic = time.time()
while True:
    #newlines = sys.stdin.readlines()
    try:
        newline = sys.stdin.readline()
    except:
        newline = None
    if newline and newline not in lines and 'RECV' in newline:
        try:
            lines = np.concatenate([lines,np.array([newline])])
            lines1sec = np.concatenate([lines1sec,np.array([newline])])
            hr,mn,sec = newline.split(' ')[0].split(':')
            hr,mn,sec = int(hr),int(mn),int(float(sec))
            linetime = time.mktime((year,mon,day,hr,mn,sec,1,0,0)) 
            #print time.time()
            #print linetime
            linetimes = np.concatenate([linetimes,np.array([linetime])])
        except:
            pass
    timenow = time.time()
    keep = linetimes > (timenow - 60.0)
    linetimes = linetimes[keep]
    lines = lines[keep]
    if int(time.time()-tic) >= 1:
        print "Packets in buffer in last 60 s: %d\tPackets in last 1 s: %d"%(lines.shape[0],lines1sec.shape[0])
    	lines1sec = np.array([])
        tic = time.time()

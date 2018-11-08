__author__ = 'fpbatta'

import sys
import time
#sys.path.append('/home/fpbatta/src/GUI/Plugins/')
sys.path.append('C:/Users/Ephys/Documents/Github/plugin-GUI/Source/Plugins/PythonPlugin/python_modules')


import numpy as npm
import signal

#import signal_plot.signal_plot

s = signal.SimplePlotter()
s.startup(20000.)

time.sleep(10)
for i in range(100):
    s.bufferfunction(npm.random.random((20,1000)))

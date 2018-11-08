import sys
import ZMQPlugins.simple_plotter_zmq

__author__ = 'fpbatta'

#sys.path.append('/Users/fpbatta/src/GUImerge/GUI/Plugins')
sys.path.append('C:\\Users\\Ephys\\Documents\\Github\\plugin-GUI\\Source\\Plugins\\PythonPlugin\\python_modules')

if __name__ == '__main__':
    pl = ZMQPlugins.simple_plotter_zmq.SimplePlotter(20000.)
    pl.startup()

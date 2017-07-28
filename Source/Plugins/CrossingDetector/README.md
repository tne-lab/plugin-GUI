# Crossing Detector Plugin

This plugin fires a TTL event when a specified input data channel crosses a specified threshold level; the criteria for detection and the output are highly customizable. It does not modify the data channels. Each instance only processes one data channel, but multiple instances can be chained together or placed in parallel.

## How it works:

* With the default settings, a positive ("rising") crossing occurs at sample _t_ if and only if sample _t_-1 is less than the threshold and sample _t_ is greater, and vice versa for a negative ("falling") crossing. However, to make it more robust to noise or just tweak it to fit a certain use case, you can also adjust the span and strictness settings:

  * __Span__ controls how many samples before or after the current sample are considered.

  * Of these, the percent that must be on the right side of the threshold is controlled by __strictness__.
  
* The duration of events, in samples, can be adjusted ("dur").

* __Timeout__ controls the minimum number of samples between two consecutive events (i.e. for this number of samples after an event fires, no more crossings can be detected).

## Installation

The crossing detector has been compiled and tested on Windows and Linux, but currently not on OSX. It shouldn't be too hard to set up though, since it's a standard plugin that requires no external libraries. Let me know if you're interested in porting it - or just go ahead and do it.

On Windows and Linux: Just clone/download this branch to get a fork of the development branch with the plugin included. Or:

* Copy this source directory to your `Source/Plugins` directory

* On Windows, also copy the `CrossingDetector` directory from `Builds/VisualStudio2013/Plugins` into the same folder in your tree. Then when you open the "Plugins" solution to compile, go to `File->Add->Existing Project...` and select the `CrossingDetector.vcxproj` file to include the plugin in your build.

I hope you find this to be useful!
-Ethan Blackwood ([@ethanbb](https://github.com/ethanbb))

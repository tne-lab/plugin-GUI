# Open Ephys GUI

Welcome to the TNEL's repository for the [Open Ephys Plugin GUI](https://github.com/open-ephys/plugin-GUI) project. We use this repo extensively in our lab and provide a few plugins that can be used with it for closed loop applications. This repo specifically uses ASIO audio drivers to greatly improve round trip latency when compared to builtin Windows audio drivers. This latency improvement enables our closed loop applications. As of the newest cmake version of the plugin-GUI, our plugins now have their own separate repositories. See [Phase Calculator](https://github.com/tne-lab/phase-calculator/tree/cmake-gui) and [Crossing Detector](https://github.com/tne-lab/crossing-detector/tree/cmake-gui) for phase based closed loop approaches to trigger stimulation within 5 degrees of a chosen target phase. See [ref](https://www.ncbi.nlm.nih.gov/pubmed/30441407). Our work in progress PythonPlugin module will be available soon to preform closed loop stimulation based on real time magnitude. We also have built a real time [Coherence Viewer](https://github.com/tne-lab/coherence-viewer) which is currently being adapted to preform broader real time TFR analyses.

To easily build our labs setup with the newest cmake version I recommend using our [oep-install](https://github.com/tne-lab/oep-installation) repository which provides scripts to easily install this version of the plugin-GUI with our plugins included.

If you are looking for an even easier to use version of the TNEL closed loop setup with our plugins, see [tnel-development](https://github.com/tne-lab/plugin-GUI/tree/tnel-development) branch which provides prebuilt binaries. This is an old version of the GUI and will no longer be updated, but is stable and ready to use. 

We are more than happy to assist you setting up our protocol! Feel free to send me (<markschatza@gmail.com>) an email with any questions.

**This version of the GUI can use Steinberg's ASIO protocol for direct sound card access and lower latency processing on Windows. To do so, before building with CMake, download the ASIO SDK from [https://www.steinberg.net/asiosdk](https://www.steinberg.net/asiosdk), unzip it alongside the GUI source tree, and rename the directory to "asiosdk" (this should contain "asio", "common", "driver", etc.). You can also set a different path to the ASIO SDK by defining ASIOSDK_PATH when calling CMake.**

**In order to use ASIO, you must also install the ASIO4ALL driver, available [here](http://www.asio4all.org/).**

![GUI screenshot](https://static1.squarespace.com/static/53039db8e4b0649958e13c7b/t/53bc11f0e4b0e16f33110ad8/1404834318628/?format=1000w)

The Open Ephys GUI is designed to provide a fast and flexible interface for acquiring and visualizing data from extracellular electrodes. Compatible data acquisition hardware includes:
- [Open Ephys Acquisition Board](http://www.open-ephys.org/acq-board/) (supports up to 512 channels)
- [Intan RHD2000 Evaluation System](http://intantech.com/RHD2000_evaluation_system.html) (supports up to 256 channels)
- [Intan Recording Controller](http://intantech.com/recording_controller.html) (supports up to 1024 channels)
- [Neuropixels Probes](http://www.open-ephys.org/neuropixels/) (supports up to 6144 channels)

The GUI is based around a *true plugin architecture*, meaning the data processing modules are compiled separately from the main application. This greatly simplifies the process of adding functionality, since new modules can be created without the need to re-compile the entire application.

Our primary user base is scientists performing electrophysiology experiments with tetrodes or silicon probes, but the GUI can also be adapted for use with other types of sensors.

[![docs](https://img.shields.io/badge/docs-confluence-blue.svg)](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/491527/Open+Ephys+GUI)
[![latest release](https://img.shields.io/github/release/open-ephys/plugin-gui.svg)](https://github.com/open-ephys/plugin-GUI/releases)
![Linux](https://github.com/open-ephys/plugin-GUI/workflows/Linux/badge.svg)
![OSX](https://github.com/open-ephys/plugin-GUI/workflows/macOS/badge.svg)
![Windows](https://github.com/open-ephys/plugin-GUI/workflows/Windows/badge.svg)
![language](https://img.shields.io/badge/language-c++-blue.svg)
[![license](https://img.shields.io/badge/license-GPL3-blue.svg)](https://github.com/open-ephys/plugin-GUI/blob/master/Licenses/Open-Ephys-GPL-3.txt)

## Installation

The easiest way to get started is to use the pre-compiled binaries for your platform of choice (links will download a .zip file, which contains a folder with the GUI executable):
- [macOS](https://github.com/open-ephys-GUI-binaries/open-ephys/archive/mac.zip)
- [Linux (64-bit)](https://github.com/open-ephys-GUI-binaries/open-ephys/archive/linux.zip)
- [Windows (7 & 10)](https://github.com/open-ephys-GUI-binaries/open-ephys/archive/windows.zip)

The Neuropixels version of the GUI is currently only available for Windows:
- [Open Ephys for Neuropixels](https://github.com/open-ephys-gui-binaries/open-ephys/tree/neuropix)

To compile the GUI from source, follow the instructions on our wiki for [macOS](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/491555/macOS), [Linux](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/491546/Linux), or [Windows](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/491621/Windows).

## How to contribute

The GUI is written in C++ with the help of the [Juce](https://juce.com/) framework. Juce includes a variety of classes for audio processing, which have been co-opted to process neural data. It might be necessary to create custom data processing classes in the future, but for now, Juce takes care of a lot of the messy bits involved in analyzing many parallel data streams.

Before you contribute, you'll need to have some familiarity with C++, as well as makefiles (Linux), Xcode (macOS), or Visual Studio (Windows) for building applications.

The recommended way to add new features to the GUI is by building a new plugin. Instructions on creating plugins can be found [here](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/46596122/Plugin+build+files). New plugin developers can publish links to their work in [this list](https://open-ephys.atlassian.net/wiki/display/OEW/Third-party+plugin+repositories) to make them available to the general public.

If you'd like to make changes to code found in this repository, please submit a pull request to the **development** branch. Adding new files to the core GUI must be done through the "Projucer," using the "open-ephys.jucer" file. The Projucer makefiles are located in the Projucer/Builds folder, or as part of the [Juce source code](https://github.com/WeAreROLI/JUCE/tree/master/extras/Projucer).






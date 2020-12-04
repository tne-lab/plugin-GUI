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
- [Neuropixels Probes](http://www.open-ephys.org/neuropixels/) (Windows only, supports up to 6144 channels)

The GUI is based around a *true plugin architecture*, meaning the data processing modules are compiled separately from the main application. This greatly simplifies the process of adding functionality, since new modules can be created without the need to re-compile the entire application.

Our primary user base is scientists performing electrophysiology experiments with tetrodes or silicon probes, but the GUI can also be adapted for use with other types of sensors.

[![docs](https://img.shields.io/badge/docs-open--ephys.github.io-blue.svg)](https://open-ephys.github.io/gui-docs/)
[![latest release](https://img.shields.io/github/release/open-ephys/plugin-gui.svg)](https://github.com/open-ephys/plugin-GUI/releases)
![Linux](https://github.com/open-ephys/plugin-GUI/workflows/Linux/badge.svg)
![OSX](https://github.com/open-ephys/plugin-GUI/workflows/macOS/badge.svg)
![Windows](https://github.com/open-ephys/plugin-GUI/workflows/Windows/badge.svg)
![language](https://img.shields.io/badge/language-c++-blue.svg)
[![license](https://img.shields.io/badge/license-GPL3-blue.svg)](LICENSE)

## Important Information

- The Open Ephys GUI is free, collaboratively developed, open-source software for scientific research. It includes many features designed to make extracellular electrophysiology data easier to acquire; however, it is not guaranteed to work as advertised. Before you use it for your own experiments, you should *test any capabilities you plan to use.* The use of a plugin-based architecture provides the flexibility to customize your signal chain, but it also makes it difficult to test every possible combination of processors in advance. Whenever you download or upgrade the GUI, be sure to test your desired configuration in a "safe" environment before using it to collect real data.

- If you observe any unexpected behavior, *please [report an issue](https://github.com/open-ephys/plugin-GUI/issues) as soon as possible.*  We rely on help from the community to ensure that the GUI is functioning properly.

- Any publications based on data collected with the GUI should cite the following article: [Open Ephys: an open-source, plugin-based platform for multichannel electrophysiology](https://iopscience.iop.org/article/10.1088/1741-2552/aa5eea). Citations remain essential for measuring the impact of scientific software, so be sure to include references for any open-source tools that you use in your research!

## Installation

The easiest way to get started is to download the installer for your platform of choice:

- [Windows](https://dl.bintray.com/open-ephys-gui/Release-Installer/Install-Open-Ephys-GUI-v0.5.3.exe) (Neuropixels plugins available via File -> Plugin Installer)
- [Ubuntu/Debian](https://dl.bintray.com/open-ephys-gui/Release-Installer/open-ephys-gui-v0.5.3.deb)
- [macOS](https://dl.bintray.com/open-ephys-gui/Release-Installer/Open_Ephys_GUI_v0.5.3.dmg)

It’s also possible to obtain the binaries as a .zip file for [Windows](https://dl.bintray.com/open-ephys-gui/Release/open-ephys-v0.5.3-windows.zip), [Linux](https://dl.bintray.com/open-ephys-gui/Release/open-ephys-v0.5.3-linux.zip), or [Mac](https://dl.bintray.com/open-ephys-gui/Release/open-ephys-v0.5.3-mac.zip).

Detailed installation instructions can be found [here](https://open-ephys.github.io/gui-docs/User-Manual/Installing-the-GUI.html).

To compile the GUI from source, follow the instructions on our wiki for [macOS](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/491555/macOS), [Linux](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/491546/Linux), or [Windows](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/491621/Windows).

## Funding

The Open Ephys GUI was created by scientists in order to make their experiments more adaptable, affordable, and enjoyable. Therefore, much of the development has been indirectly funded by the universities and research institutes where these scientists work, especially MIT, Brown University, and the Allen Institute for Brain Science.

Since 2014, the support efforts of [Aarón Cuevas López](https://github.com/aacuevas) have been funded by revenue from the [Open Ephys store](https://open-ephys.org/store), via a contract with Universidad Miguel Hernández in Valencia.

Since 2019, the support efforts of [Pavel Kulik](https://github.com/medengineer) and [Anjal Doshi](https://github.com/anjaldoshi) have been funded by a BRAIN Initiative U24 Award to the Allen Institute ([U24NS109043](https://projectreporter.nih.gov/project_info_description.cfm?aid=9645567)).

## How to contribute

We welcome bug reports, feature recommendations, pull requests, and plugins from the community. For more information, see [Contributing to the Open Ephys GUI](CONTRIBUTING.md).

If you have the potential to donate money or developer time to this project, please get in touch via info@open-ephys.org. There are plenty of opportunities to get involved.



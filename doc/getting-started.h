/******************************************************************************
 * Copyright (C) 2011  Intel Corporation. All rights reserved.
 *****************************************************************************/
/*!
 * @file getting-started.h
 *
 * @section introduction Introduction
 * 
 * This document describes the steps required to get provman running on
 * MeeGo netbook.  
 *
 * <ol>
 * <li>Setup netbook</li>
 * <li>Install depdendencies</li>
 * <li>Download code</li>
 * <li>Compile and install code</li>
 * <li>Enable phonesim modem</li>
 * <li>Run some tests</li>
 * 
 * @section dependencies Install Dependencies:
 *
 * We're actually going to compile the provman code and some of the
 * dependencies directly on our netbook.  To do this we need to install
 * some extra packages, namely git, gcc and some development libraries.
 * Execute the following commands.
 *
 * \code
 * sudo zypper install doxygen (if you want to build the docs)
 * sudo zypper install gcc-c++
 * sudo zypper install git
 * sudo zypper install glib2-devel
 * sudo zypper install evolution-data-server-devel
 * sudo zypper install libqt-devel (Required by phonesim)
 * \endcode
 *
 *
 * @section download Download the Code 
 * First let's download ofono and phonesim.  These packages are only
 * needed if you want to test the oFono telephony plugin.
 * They are both hosted on kernel.org.  Note that we are not
 * going to compile and install oFono.  It is already running on your
 * netbook.  We just need the python test scripts stored in the oFono
 * source repository to enable the virtual phone modem.  I put all my code
 * in a directory called 'code', so
 *
 * \code
 * mkdir code
 * cd code
 *
 * git clone git://git.kernel.org/pub/scm/network/ofono/ofono.git
 * git clone git://git.kernel.org/pub/scm/network/ofono/phonesim.git
 * \endcode
 *
 * If you haven't done so already you now need to download provman. 
 * Provman is hosted on github.  To download type
 * 
 * \code
 * git://github.com/otcshare/provman.git
 * \endcode
 * 
 * @section compiling Compiling the Code
 *
 * Let's start with phonesim.  
 * \code
 * cd code/phonesim
 * ./bootstrap-configure
 * make
 * \endcode
 * 
 * I usually don't bother to install phonesim.  Instead, I just run it from
 * the source directory as explained in the HACKING file.
 *
 * Now lets compile provman,
 *
 * \code
 * cd code/provman
 * autoreconf -i
 * CFLAGS="-g" ./configure --enable-logging
 * make
 * sudo make install
 * \endcode
 *
 * It's best to compile with debugging and logging for the time being as there
 * are likely to be lots of bugs.  The log file is stored in /tmp/provman.log.
 *
 * @section phonesim Enabling Phonesim modem
 * 
 * If you do not want to test telephony settings or you have plugged a real
 * modem into the netbook, you can skip this section.  Otherwise, read on.
 * Edit the file /etc/ofono/phonesim.conf with your favourite editor, e.g.,
 * 
 * \code
 * sudo vi /etc/ofono/phonesim.conf
 * \endcode
 * 
 * This needs to be done as root.  Then uncomment all the phonesim
 * settings.  The main part of the file should look like this.
 *
 * \code
 * [phonesim]
 * Address=127.0.0.1
 * Port=12345
 * \endcode
 *
 * Save the file and then reboot the device.  I don't know of any way to get
 * oFono to re-read its configuration files after it has been started.
 *
 * Start the terminal application and launch phonesim as recommended in the
 * phonesim HACKING document
 * 
 * \code
 * cd code/phonesim
 * ./src/phonesim -p 12345 -gui src/default.xml
 * \endcode
 *
 * Nothing will appear on the screen until you enable your modem, so
 * start a new terminal window, Ctrl+Shift+t, and then type the following
 * commands
 *
 * \code
 * cd code/ofono/test
 * ./enable-modem
 * ./online-modem
 * \endcode
 *
 * If everything works okay the phonesim UI should appear.
 *
 * @section test Quick Test
 * 
 * As a quick test to see if everything is working cd to the 
 * ~code/provman/testcases directory and type
 * \code
 * ./get-all-system /
 * \endcode
 * A list of all the manageable system settings should be output to the console.
 *
 ******************************************************************************/


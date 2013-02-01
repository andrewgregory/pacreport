.. Copyright (C) 2013 Andrew Gregory <andrew.gregory.8@gmail.com>
.. See the COPYING file for copying permissions.

pacreport
=========

Generates a comprehensive package report for Arch Linux.

Report features:

+ top level packages installed explicitly
+ top level packages installed as dependencies
+ installed packages not in a repository
+ packages missing from base and base-devel groups
+ missing package files
+ cache directory sizes
+ packages sizes include dependencies not needed by other packages (like
  ``pacman --recursive``)

TODO
----

+ files not owned by any packages
+ pacman save files
+ broken symlinks
+ config file

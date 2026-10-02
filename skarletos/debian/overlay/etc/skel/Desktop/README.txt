SkarletOS (Debian edition) quick start
--------------------------------------
Alt+F1 or the Windows key   launcher: apps, places, power
Alt+F2                      runner: apps, files, maths (6*7), commands
Alt+F12                     desktop menu: widgets and activities
Alt+Tab / Alt+F4            next window / close window
Ctrl+F1..F4                 virtual desktops
The mouse works too: drag title bars, click the panel.

Installing software (in Skarlet Terminal):
  sudo apt update               refresh the list of software
  apt search WORD               find programs
  sudo apt install NAME         install, e.g. firefox-esr, gimp, vlc
  sudo apt remove NAME          uninstall
Installed programs appear in the launcher by themselves.

The live system forgets everything at shutdown.  To keep your
programs and files, install SkarletOS to the disk:
  sudo skarlet-install

PFO 50 v0.2.1
=============

PFO 50 (Play Forever Online 50) is a mod for UFO 50 that adds online multiplayer
support to UFO 50 v1.7.6.

Installation
------------
1. Extract all the files from the zip to a new folder
2. Right click UFO 50 in Steam and select "Properties..."
3. From the "Installed Files" tab select "Browse..." to open the game folder
3. Find the file "data.win" in the game folder and copy it to the folder you
   extracted earlier
4. Run DeltaPatcher (included) and choose "data.win" (from the extracted folder)
   as the Original file and the extracted "data.xdelta" file as the XDelta patch
5. Click "Apply patch", which will overwrite the "data.win" file with the
   patched one
6. Copy both "data.win" and "PFO.dll" from the extracted folder to the game
   folder, overwriting any existing files
7. Run UFO 50, if the mod has been successfully installed you should see
   "PFO 50" and the correct version number in the lower left corner

If you later want to revert back to unmodded UFO 50, you can do "Verify
integrity of game files" from the "Installed Files" tab from step 1. To
reinstall the mod again, you can simply copy "data.win" and "PFO.dll" from the
extracted folder to the game folder (no need to run DeltaPatcher again).

Controls
--------
While playing online, Press the number keys (0-9) to set the input delay for
both players
To set the input delay for just yourself, press Shift + a number key
Press F1-F4 to show or hide on screen information while online

Input Delay
-----------
PFO 50 sends your inputs to the other player instantly, but they can be used at
a delay to ensure that both players receive them just in time. The delay needed
depends on the ping between the players, so if you have a lower ping you can use
a lower delay. Try to find the lowest delay where the game runs smoothly at 60
FPS.

Input delays are additive, so if a delay of 3 for both players runs smoothly,
then a delay of 0 for one player and 6 for the other will run equally smoothly.
Depending on the game, you may wish to have equal input delay, or one player
taking all of the input delay. You can use the number keys to set the input
delay for both players, and you can hold Shift while pressing number keys to
set it for only yourself (allowing each player to have a separate delay).

Credits
-------
Blazier - programming, art
Sailor - testing
p-sam - ufo50-patcher

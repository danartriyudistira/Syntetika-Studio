#!/bin/sh
# requires appdmg, which can be installed via: npm install -g appdmg

rm Syntetika-Mac.dmg

rm -rf Syntetika.app
ditto ../../ignore/build/Source/Syntetika_artefacts/Release/Syntetika.app Syntetika.app

#codesign --force --options runtime --timestamp --verbose=4 --entitlements entitlements.plist --sign "Developer ID Application: Ryan Challinor (J5RJ562GN5)" Syntetika.app/Contents/MacOS/Syntetika
#codesign --force --options runtime --timestamp --verbose=4 --entitlements entitlements.plist --sign "Developer ID Application: Ryan Challinor (J5RJ562GN5)" Syntetika.app/Contents/Frameworks/Python.framework/Versions/3.9/Python
#codesign --force --options runtime --timestamp --verbose=4 --entitlements entitlements.plist --sign "Developer ID Application: Ryan Challinor (J5RJ562GN5)" Syntetika.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9
#codesign --force --options runtime --timestamp --verbose=4 --entitlements entitlements.plist --sign "Developer ID Application: Ryan Challinor (J5RJ562GN5)" Syntetika.app/Contents/Frameworks/Python.framework/Versions/3.9/Resources/Python.app/Contents/MacOS/Python

appdmg SYNTETIKA_dmg.json Syntetika-Mac.dmg

codesign -s "Developer ID Application: Ryan Challinor (J5RJ562GN5)" --timestamp Syntetika-Mac.dmg

"C:\Program Files (x86)\WiX Toolset v3.11\bin\heat" dir ..\ignore\build\Source\Syntetika_artefacts\Release\resource -o HarvestedResourceDir.wxs -scom -frag -srd -sreg -gg -cg SyntetikaResourceDir -dr RESOURCE_DIR_REF
"C:\Program Files (x86)\WiX Toolset v3.11\bin\heat" dir ..\ignore\build\Source\Syntetika_artefacts\Release\python -o HarvestedPythonDir.wxs -scom -frag -srd -sreg -gg -cg SyntetikaPythonDir -dr PYTHON_DIR_REF
"C:\Program Files (x86)\WiX Toolset v3.11\bin\candle" Syntetika.wxs HarvestedResourceDir.wxs HarvestedPythonDir.wxs -arch x64
"C:\Program Files (x86)\WiX Toolset v3.11\bin\light" Syntetika.wixobj HarvestedResourceDir.wixobj HarvestedPythonDir.wixobj -b ..\ignore\build\Source\Syntetika_artefacts\Release\resource -b ..\ignore\build\Source\Syntetika_artefacts\Release\python -out Syntetika-Windows.msi -ext WixUIExtension

@pause

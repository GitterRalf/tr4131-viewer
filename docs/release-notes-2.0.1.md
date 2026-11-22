## TR4131 Viewer 2.0.1

Die 32-Bit-Ausgabe für den Advantest TR4131 kommt in diesem Release hinzu. Beide Installer enthalten die jeweils passenden Qt-, MinGW- und Qwt-Laufzeitdateien.

| Windows-Ausgabe | Installer | Qt und Compiler |
| --- | --- | --- |
| 32 Bit (x86) | TR4131-Viewer-Setup-2.0.1-x86.exe | Qt 5.15.19, MinGW 8.1 |
| 64 Bit (x64) | TR4131-Viewer-Setup-2.0.1-x64.exe | Qt 6.11.0, MinGW 13.1 |

- Die 32-Bit-Ausgabe kann auf 64-Bit-Windows parallel installiert werden. Sie verwendet einen eigenen Installationsordner und eigene Verknüpfungen.
- Frequenzdiagramm, Spektrogramm, 3D-Ansicht, PNG- und CSV-Export sowie Deutsch, Englisch und Französisch sind in beiden Ausgaben enthalten. Die 3D-Ansicht nutzt OpenGL.
- Für GPIB den NI-488.2-Treiber mit Unterstützung für 32-Bit- beziehungsweise 64-Bit-Anwendungen verwenden. Beim Elektor USB-GPIB-Adapter wird der virtuelle COM-Port-Treiber separat installiert.
- Die Update-Suche für das private Repository verwendet eine angemeldete GitHub CLI.

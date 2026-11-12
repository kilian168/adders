# VHDL-Dateien der Schaltnetze (4 Stellen)

| Datei | Inhalt |
|---|---|
| `ter_adder_4bit.vhdl` | SD-Addierer, unveraenderter Export aus Digital (von RapidRTL nicht lesbar) |
| `ter_adder_4bit_rapidrtl.vhdl` | SD-Addierer, fuer RapidRTL angepasst (Volladdierer `full_adder` statt `DIG_Add`); Quelle von `abb_sd_schaltnetz.png` |
| `sdtc_adder_4bit_rapidrtl.vhdl` | SD/TC-Addierer (PM/PPM-Zelle), fuer RapidRTL angepasst |
| `abb_sd_schaltnetz.png` | Schaltbild des SD-Addierers (RapidRTL-Export, fuer die Arbeit invertiert) |

Die Digital-Exporte nicht ueberschreiben; die `*_rapidrtl.vhdl`-Dateien sind die Vorlagen fuer die Schaltbilder in der Arbeit.

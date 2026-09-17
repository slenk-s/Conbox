@echo off
G:\keil5\ARM\5.06_SLENK\bin\armlink.exe --cpu Cortex-M0+ build_tmp\*.o --library_type=microlib --strict --scatter MDK-ARM\Objects\T1.sct --summary_stderr --info summarysizes --map --load_addr_map_info --xref --info sizes --info totals --info unused --info veneers --list build_tmp\T1.map -o build_tmp\T1.axf

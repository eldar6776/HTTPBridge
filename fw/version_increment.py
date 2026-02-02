import datetime

FILENAME_VERSION = "version.h"
BUILD_NUMBER_FILE = "build_no.txt"

# Čitanje build broja
try:
    with open(BUILD_NUMBER_FILE, 'r') as f:
        build_no = int(f.read().strip()) + 1
except FileNotFoundError:
    build_no = 1

# Pisanje novog build broja nazad u fajl
with open(BUILD_NUMBER_FILE, 'w') as f:
    f.write(str(build_no))

# Generisanje version.h fajla
header_content = f"""
#ifndef VERSION_H
#define VERSION_H

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH {build_no}
#define BUILD_DATE "{datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}"

#endif
"""

with open("include/" + FILENAME_VERSION, 'w') as f:
    f.write(header_content)
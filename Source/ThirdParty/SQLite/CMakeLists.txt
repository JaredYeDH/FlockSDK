#
# Copyright (c) 2008-2017 Flock SDK developers & contributors. 
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

# Define target name
set (TARGET_NAME sqlite)

# https://www.sqlite.org/compile.html
add_definitions (-DSQLITE_USE_URI=1 -DSQLITE_ENABLE_COLUMN_METADATA -DSQLITE_SOUNDEX)

# Define source files
set (SOURCE_FILES src/sqlite3.c)

# Setup target
setup_library ()

# Install headers for building and using the Flock library
install_header_files (DIRECTORY src/ DESTINATION ${DEST_INCLUDE_DIR}/ThirdParty/SQLite FILES_MATCHING PATTERN *.h)  # Note: the trailing slash is significant

# Setup additional SQLite CLI standalone target (this target can be transfered and executed on an embedded device, such as Raspberry Pi and Android)
# Define target name for SQLite shell
set (TARGET_NAME isql)

# Define source files
set (SOURCE_FILES src/shell.c src/sqlite3.c src/sqlite3.h)

# Define dependency libs
if (NOT WIN32)
    set (LIBS dl)
    if (READLINE_FOUND)
        add_definitions (-DHAVE_READLINE)
        list (APPEND INCLUDE_DIRS ${READLINE_INCLUDE_DIRS})
        list (APPEND LIBS ${READLINE_LIBRARIES})
    endif ()
endif ()

# Setup target
setup_executable (NODEPS TOOL)

# SQLite will not work correctly with the -ffast-math option of GCC
string (REPLACE -ffast-math "" CMAKE_C_FLAGS ${CMAKE_C_FLAGS})

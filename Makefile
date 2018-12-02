# -------------------------------------------------------------------------------
#  Copyright (c) Nate Simon
#
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
# -------------------------------------------------------------------------------

.PHONY: all clean

PROJECT_BIN := miditokb
PROJECT_INPUT := MidiToKb.c

CC := gcc
CFLAGS := -Wall -Werror
ifeq ($(DEBUG),1)
CFLAGS += -g -DDEBUG=1
endif
LDFLAGS := -lasound


$(PROJECT_BIN): $(PROJECT_INPUT)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

all: $(PROJECT_BIN)

clean:
	@rm $(PROJECT_BIN)

# Copyright (C) 2018  Christian Berger
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

###########################################################################
# Find libOpenMaxIL.
FIND_PATH(OPENMAXIL_INCLUDE_DIR
          NAMES IL/OMX_Core.h
          PATHS /usr/local/include/
                /usr/include/
                /opt/vc/include/)
MARK_AS_ADVANCED(OPENMAXIL_INCLUDE_DIR)
FIND_LIBRARY(OPENMAXIL_LIBRARY
             NAMES openmaxil
             PATHS ${LIBOPENMAXILDIR}/lib/
                    /usr/lib/arm-linux-gnueabihf/
                    /usr/lib/arm-linux-gnueabi/
                    /usr/lib/x86_64-linux-gnu/
                    /usr/local/lib64/
                    /usr/lib64/
                    /usr/lib/
                    /opt/vc/lib/)
MARK_AS_ADVANCED(OPENMAXIL_LIBRARY)

###########################################################################
IF (OPENMAXIL_INCLUDE_DIR
    AND OPENMAXIL_LIBRARY)
    SET(OPENMAXIL_FOUND 1)
    SET(RPI_OMX_FIRMWARE openmaxil bcm_host vcos vchiq_arm vcsm)
    SET(OPENMAXIL_LIBRARIES ${OPENMAXIL_LIBRARY} ${RPI_OMX_FIRMWARE})
    SET(OPENMAXIL_INCLUDE_DIRS ${OPENMAXIL_INCLUDE_DIR})
ENDIF()

MARK_AS_ADVANCED(OPENMAXIL_LIBRARIES)
MARK_AS_ADVANCED(OPENMAXIL_INCLUDE_DIRS)

IF (OPENMAXIL_FOUND)
    MESSAGE(STATUS "Found libOpenMaxIL: ${OPENMAXIL_INCLUDE_DIRS}, ${OPENMAXIL_LIBRARIES}")
ELSE ()
    MESSAGE(STATUS "Could not find libOpenMaxIL")
ENDIF()

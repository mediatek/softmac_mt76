cmake_minimum_required(VERSION 2.8)

PROJECT(mt76-test C)
ADD_DEFINITIONS(-Os -Wall -Werror --std=gnu99 -g3)

UNSET(backports_dir CACHE)
FIND_PATH(
	backports_dir
	NAMES "mac80211/uapi/linux"
)
INCLUDE_DIRECTORIES("${backports_dir}/mac80211/uapi")

ADD_EXECUTABLE(mt76-test main.c fields.c eeprom.c fwlog.c)
TARGET_LINK_LIBRARIES(mt76-test nl-tiny)

SET(CMAKE_INSTALL_PREFIX /usr)

INSTALL(TARGETS mt76-test
	RUNTIME DESTINATION sbin
)

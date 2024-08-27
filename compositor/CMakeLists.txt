# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2024  Panasonic Automotive Systems, Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

if(TARGET_ENV STREQUAL "x11")
    set(WINSYS_SRC "winsys_x11")
    pkg_check_modules(WINSYS REQUIRED x11)

elseif(TARGET_ENV STREQUAL "xcb")
    set(WINSYS_SRC "winsys_xcb")
    pkg_check_modules(WINSYS REQUIRED x11 x11-xcb xcb xcb-keysyms)

elseif(TARGET_ENV STREQUAL "wayland")
    set(WINSYS_SRC "winsys_wayland")
    pkg_check_modules(WINSYS REQUIRED wayland-egl wayland-client)
    add_definitions(-DUSE_TOUCH)
elseif(TARGET_ENV STREQUAL "drm")
    set(WINSYS_SRC "winsys_drm")
    pkg_check_modules(WINSYS REQUIRED libdrm libinput libsystemd libudev gbm)
    add_definitions(-DUSE_TOUCH)
endif()
set(WINSYS_INCLUDE_DIRS "${WINSYS_INCLUDE_DIRS}")
message(STATUS "WINSYS_SRC is set to ${WINSYS_SRC}")

add_executable(rvgpu-wlproxy
        ../common/assertgl.c
	../common/assertegl.c
	../common/util_egl.c
	../common/util_shader.c
	../common/util_render2d.c
	../common/util_env.c
	../common/winsys/${WINSYS_SRC}.c
        ../third_party/wayland/protocols/wayland-protocol.c
	../third_party/wayland/protocols/xdg-shell-protocol.c
        wayland_seat.c
	main.c
	)
target_include_directories(rvgpu-wlproxy
        PRIVATE
	../common
	../common/winsys
	../third_party/wayland/protocols
	${WINSYS_INCLUDE_DIRS}
	${extlibs_INCLUDE_DIRS}
        )
 
target_compile_definitions(rvgpu-wlproxy PRIVATE _GNU_SOURCE)
target_link_libraries(rvgpu-wlproxy
        PRIVATE ${extlibs_LIBRARIES} ${WINSYS_LIBRARIES} pthread m xkbcommon)
install(TARGETS rvgpu-wlproxy RUNTIME DESTINATION bin)

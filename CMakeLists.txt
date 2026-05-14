cmake_minimum_required(VERSION 3.18)

project(kuni
        VERSION 0.0.2 # CI_PROJECT_VERSION
    )

# Compiler version check (see README for requirements)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
        message(FATAL_ERROR "Kuni requires GCC 15+. Found: ${CMAKE_CXX_COMPILER_VERSION}. Please upgrade your compiler.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
        message(FATAL_ERROR "Kuni requires Clang 20+. Found: ${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER_ID}). Please upgrade your compiler.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.28)
        message(FATAL_ERROR "Kuni requires MSVC 19.28+. Found: ${CMAKE_CXX_COMPILER_VERSION}. Please upgrade your compiler.")
    endif()
else()
    message(WARNING "Unknown compiler: ${CMAKE_CXX_COMPILER_ID}. Build may fail if compiler does not support C++20 coroutines.")
endif()




option(BUILD_SHARED_LIBS OFF)

set(AUI_VERSION v8.0.0-rc.23)

# Use AUI.Boot
include(aui.boot.cmake)

set(AUI_COROUTINES "Use C++20 coroutines" ON)
auib_mark_var_forwardable(AUI_COROUTINES)

# import AUI
auib_import(aui https://github.com/aui-framework/aui
        COMPONENTS core json curl crypt image views
        VERSION ${AUI_VERSION})


auib_import(toml11 https://github.com/ToruNiina/toml11
        VERSION v4.4.0
        CMAKE_ARGS -DTOML11_ENABLE_ACCESS_CHECK=ON
)

auib_import(prometheus-cpp https://github.com/jupp0r/prometheus-cpp
            VERSION v1.3.0
            CMAKE_ARGS -DENABLE_PUSH=OFF -DENABLE_COMPRESSION=OFF -DENABLE_TESTING=OFF
)

auib_import(date https://github.com/HowardHinnant/date)

set(AUIB_TD_VALIDATE OFF)
auib_import(Td https://github.com/tdlib/td
        VERSION 0ae923c493bceb75433de2682ba8ae29cc7bf88d)

aui_executable(${PROJECT_NAME})

# Link required libs
aui_link(${PROJECT_NAME} PUBLIC
        aui::core
        aui::json
        aui::curl
        aui::crypt
        aui::image
        aui::views
        Td::TdStatic
        toml11::toml11
        prometheus-cpp::pull)

aui_compile_assets(${PROJECT_NAME})
aui_enable_tests(${PROJECT_NAME})

# Setup icon, display name, etc
aui_app(TARGET ${PROJECT_NAME}
        NAME "Kuni"
        VENDOR "Alex2772"
        ICON "assets/img/icon.svg"
)

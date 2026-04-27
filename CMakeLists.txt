cmake_minimum_required(VERSION 3.18)
project(kuni VERSION 1.0.0)

option(BUILD_SHARED_LIBS OFF)

set(AUI_VERSION v8.0.0-rc.21)

# Use AUI.Boot
file(
        DOWNLOAD
        https://raw.githubusercontent.com/aui-framework/aui/${AUI_VERSION}/aui.boot.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/aui.boot.cmake)
include(${CMAKE_CURRENT_BINARY_DIR}/aui.boot.cmake)

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

auib_import(Boost https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.xz
        ARCHIVE
        CMAKE_ARGS -DBOOST_ENABLE_CMAKE=ON
)

auib_import(Crow https://github.com/crowcpp/crow
    VERSION 21f8bcfa6b900fcc76ee01a5bc6823b129f1e63f
    REQUIRES Boost
    CMAKE_ARGS -DCROW_USE_BOOST=ON
)

auib_import(date https://github.com/HowardHinnant/date)

set(AUIB_TD_VALIDATE OFF)
auib_import(Td https://github.com/tdlib/td
        VERSION 0ae923c493bceb75433de2682ba8ae29cc7bf88d)

aui_executable(${PROJECT_NAME})

# Link required libs
aui_link(${PROJECT_NAME} PUBLIC aui::core aui::json aui::curl aui::crypt aui::image aui::views Td::TdStatic toml11::toml11 Crow::Crow)

aui_enable_tests(${PROJECT_NAME})

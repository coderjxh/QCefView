# set folder
set(CMAKE_FOLDER "QCefView/Core")

# set CEF version to be used
# https://cef-builds.spotifycdn.com/index.html
# https://bitbucket.org/chromiumembedded/cef/wiki/BranchesAndBuilding.md
set(CEF_SDK_VERSION "144.0.26+g8b05ce5+chromium-144.0.7559.252")

# add CefViewCore project
add_subdirectory(CefViewCore/CefViewCore-main)

set(CefViewCore_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CefViewCore/CefViewCore-main")

configure_file(
    "${CefViewCore_SOURCE_DIR}/include/CefVersion.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/CefVersion.h"
    COPYONLY
)
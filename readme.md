# HelloOpenXrAndOpenVr

Minimal test program that implements similar functionality to hello_xr using the OpenXR and the OpenVR APIs

Prompts user for the API
1) OpenXR or OpenVR

Uses:
* 64 bit builds because currently this is the build that works for me with OpenXr and SteamVr backend   	
* OpenGL renderer to keep things more portable


Then shows the cube thing


The purpose is to:
1. See if it's possible
2. find out how different the code looks
3. Learn some unexpected things

# Status
WIP - at the moment it just compiles and links


# Project configuration notes
To get OpenGL references to compile and link need to:
* set XR_USE_GRAPHICS_API_OPENGL as a preprocessor symbol 
* add opengl32.lib
* add externals/gfxwrapper and it's associated utils

# Building
The OpenXR library needs to be built in two steps
1. cmake to build the solution:

    cd src/submodules
    mkdir OpenXR-SDK-build
    cd OpenXR-SDK-build
    cmake -G "Visual Studio 15 Win64" ../OpenXR-SDK

2. visual studio to build the loader: 
In it's own instance of Visual studio, open up 
    C:\projects\HelloOpenXrAndVr\src\submodules\OpenXR-SDK-build\OPENXR.sln
    build solution
    to create submodules\OpenXR-SDK-build\src\loader\Debug\openxr_loaderd.lib
 




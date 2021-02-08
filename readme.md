# HelloOpenXrAndOpenVr

Minimal test program that implements similar functionality to hello_xr using the OpenXR and the OpenVR APIs

Prompts user for the API
1) OpenXR or OpenVR

Then shows the cube thing


The purpose is to:
1. See if it's possible
2. find out how different the code looks
3. Learn some unexpected things

# Status
WIP - at the moment it just compiles and links


# Building
The OpenXR library needs to be built in two steps
1. cmake to build the solution
    cd src/submodules
    mkdir OpenXR-SDK-build
    cd OpenXR-SDK-build
    cmake -G "Visual Studio 15" ../OpenXR-SDK
2. visual studio to build the loader 
In it's own instance of Visual studio, open up 
    C:\projects\HelloOpenXrAndVr\src\submodules\OpenXR-SDK-build\OPENXR.sln
    build solution
    to create submodules\OpenXR-SDK-build\src\loader\Debug\openxr_loaderd.lib
 




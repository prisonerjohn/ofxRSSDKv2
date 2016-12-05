# RSSDK addon for openframeworks 0.9.8 with support for F200, R200, and SR300

[Download Intel RealSense SDK 2016 R2 here](https://software.intel.com/en-us/intel-realsense-sdk)

Tested Windows 10, Visual Studio Community 2015

__To create new project:__

* use project generator, include ofxRSSDKv2 addon, open project in VS2015
* right click on project in Solution Explorer, select Properties
* change "Configuration" to "All Configurations" and "Platform" to "All Platforms"
* under "C\C++" > "General" > "Additional Include Directories", add:  
$(RSSDK_DIR)include;
* under "Linker" > "General" > Additional Library Directories", add:  
$(RSSDK_DIR)lib\$(Platform);

example:  
![additional include and library directories](https://raw.githubusercontent.com/tyhenry/ofxRSSDKv2/master/readme_addDirs.png)


## Fork updates:

__To-do:__

* Check compatibility with 0.9.8, update example projects as needed
* Face tracking & landmarks
* Example with mesh & texture
...


---

__Under Construction__

Currently supported:
* RGB Streaming
* Depth Streaming (Raw Depth and Depth as Color)
* Point Cloud

If you've come here from the previous version, you'll notice the interface has completely changed, but hopefully it's still fairly easy to understand. Thanks everyone for your patience and please feel free to continue sending me feature requests, I don't get to everything right away as you can tell, but it definitely helps me prioritize. Cheers! -Seth

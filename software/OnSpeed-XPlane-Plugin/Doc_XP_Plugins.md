# About X-Plane Plugins

A plugin is executable code that runs inside X-Plane, extending what X-Plane does. Plugins are modular, allowing developers to extend the simulator without having access to the simulator's source code.

## links

- https://developer.x-plane.com/article/building-and-installing-plugins/


- https://developer.x-plane.com/sdk/
- https://developer.x-plane.com/sdk/XPLMDataAccess/
- https://developer.x-plane.com/sdk/XPLMProcessing/
- https://developer.x-plane.com/sdk/XPLMPlugin/


## What Plugins Can Do

Plugins are executable code that runs inside X-Plane. They allow you to:

- Run code from inside the simulator, either continually or in response to events
- Read data from the simulator (e.g., reading flight instrument values)
- Modify data in the simulator, changing its actions or behavior
- Create user interface elements inside the sim (e.g., popup windows with instruments)
- Control various subsystems of the simulator

## Plugins and DLLs

### About DLLs
A dynamically linked library (DLL) is a set of compiled functions/procedures and their associated variables. Key characteristics:

- DLLs don't contain a "main" function - they contain functions that other programs call
- DLLs have a directory of "exported" functions that other programs can access
- Programs link to DLLs at runtime, not compile time
- Multiple programs can share one DLL

### Plugin Architecture 
The X-Plane plugin system uses:

- X-Plane (host application)
- X-Plane Plugin Manager (XPLM) - Central DLL hub
- Plugins (DLLs that communicate with XPLM)
- Optional additional libraries

## Plugin Concepts

### Plugin Signatures
Each plugin has a unique signature that identifies it. Format example:


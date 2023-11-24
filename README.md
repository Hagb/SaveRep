# SaveRep mod for Touhou Hisoutensoku

This mod is to save replay in one of the following situations:

- the user is p1 or p2 in network or local battle, and:
    - p1 or p2 presses ESC to end the game, or
    - the game ends before one of the players wins because of desync, or
    - the connection is lost, or
    - the window is closed, or
    - the game crashes
- the user is spectating, and:
    - the user presses ESC to stop spectating, or
    - the connection is lost
    - the window is closed, or
    - the game crashes

## Build
Requires CMake, git and the VisualStudio compiler (MSVC).
Both git and cmake needs to be in the PATH environment variable.

All the following commands are to be run inside the visual studio 32bits compiler
command prompt (called `x86 Native Tools Command Prompt for VS 20XX` in the start menu), unless stated otherwise.

## Initialization
First go inside the folder you want the repository to be in.
In this example it will be C:\Users\PinkySmile\SokuProjects but remember to replace this
with the path for your machine. If you don't want to type the full path, you can drag and
drop the folder onto the console.

`cd C:\Users\PinkySmile\SokuProjects`

Now let's download the repository and initialize it for the first time
```
git clone https://github.com/Hagb/SaveRep
cd SaveRep
git submodule init
git submodule update
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
```
Note that if you want to build in Release, you should replace `-DCMAKE_BUILD_TYPE=Debug` with `-DCMAKE_BUILD_TYPE=Release`.

## Compiling
Now, to build the mod, go to the build directory (if you did the previous step you already are)
`cd C:\Users\PinkySmile\SokuProjects\SaveRep\build` and invoke the compiler by running `cmake --build . --target SaveRep`. If you change the name of the mod (in the add_library statement in CMakeLists.txt), you will need to replace 'SaveRep' by the name of your mod in the previous command.

You should find the resulting SaveRep.dll mod inside the build folder that can be to SWRSToys.ini.
In my case, I would add this line to it `SaveRep=C:/Users/PinkySmile/SokuProjects/SaveRep/build/SaveRep.dll`.

## Thanks
Thank [Tstar](https://github.com/Tstar00) for his test and helpful feedback, as well as suggestions.

## Todo

Make the mod configurable.

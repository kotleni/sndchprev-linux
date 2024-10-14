## sndchprev
![C](https://img.shields.io/badge/c-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-%23008FBA.svg?style=for-the-badge&logo=cmake&logoColor=white)
<br>
Sound channels direction preview for games on Linux. (X11 and PipeWire only)
<br>Warning! It's very poor and unstable.
<br>Check <a href="https://github.com/kotleni/sndchprev-linux/issues">issues</a> to get more information about developing.

### Dependencies
```cmake, gcc, pipewire, x11```

### Building
```bash
git clone https://github.com/kotleni/sndchprev-linux
cd sndchprev-linux
mkdir build && cd build
cmake ..
make
```

### Example
<img src='https://github.com/kotleni/sndchprev-linux/blob/dev/assets/preview.png?raw=true' width=420>
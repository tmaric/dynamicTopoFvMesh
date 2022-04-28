# dynamicTopoFvMesh: OpenFOAM-v2112 port

This is a fork of smenon/dynamicTopoFvMesh, ported to OpenFOAM-v2112. Currently, the code only compiles, testing + debugging in progress.

The upgrade to OpenFOAM-v2112 includes: 

1. Update of the RTS structure: this is correct. 
2. Switch from Xfer to C++ move semantics: runtime errors expected (SEGFAULTS most likely), std::move was used, if it works, there might be an impact on performance efficiency, profiling will be required.
3. Updating GeometricField public typedefs: this is correct.
4. Updating GeometricField boundary field access member functions: this is correct.
5. Ignoring a bunch of deprecation warnings on still suported length checks and constant variable binding - see dynamicTopoFvMesh/Make/options and mesquiteMotionSolver/Make/options: in progress, enabling warnings and cleaning them up, currently only errors were removed.  

## Compilation 

### Obtain mesquite 

```
    dynamicTopoFvMesh> wget https://software.sandia.gov/mesquite/mesquite-2.3.0.tar.gz
```

### Configure, build and install mesquite 

More information in Install.txt.  

```
    dynamicTopoFvMesh> gunzip mesquite-2.3.0.tar.gz && tar xf mesquite-2.3.0.tar && cd mesquite-2.3.0
    mesquite-2.3.0> export CXXFLAGS=-fPIC && ./configure --prefix=$(pwd)
    mesquite-2.3.0> make && make install && cd ..
```

### Build dynamicTopoFvMesh

More information in Install.txt.  

1. Source OpenFOAM-v2112, **make sure you use the standard -std=c++2a**, edit $WM_PROJECT_DIR/wmake/rules/General/Gcc/c++

```
CC          = g++-10 -std=c++2a
```

2. Export mesquite environment variables for dynamicTopoFvMesh compilation 

```
    dynamicTopoFvMesh> export MESQUITE_DIR=$(pwd)/mesquite-2.3.0
    dynamicTopoFvMesh> export MESQUITE_LIB_DIR=$MESQUITE_DIR/lib
```

3. Build dynamicTopoFvMesh, tested with g++ (GCC) 11.2.0

```
    dynamicTopoFvMesh> ./Allwmake
```

# dynamicTopoFvMesh: original readme 

An implementation of parallel, adaptive, simplical remeshing for OpenFOAM.

## Copyright Information
    Copyright (C) 2007-2015
    Sandeep Menon
    University of Massachusetts Amherst.

## License
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

## Description

An implementation of parallel, adaptive, simplical remeshing for OpenFOAM.
The source-tree is separated into several classes for convenience:

### dynamicTopoFvMesh
Mesh class that extends dynamicFvMesh functionality to handle dynamic simplical meshes, which consist of triangle prisms in 2D, and tetrahedra in 3D. Adaptation is driven mainly by mesh-quality and mesh refinement criteria. When used in combination with mesh-smoothing methods, this functionality is expected to suit situations where domain deformation characteristics are not known a-priori. Conservative solution remapping after mesh reconnection is performed automatically. Parallel functionality is also included.

### fluxCorrector
Auxiliary library which is used by dynamicTopoFvMesh to perform a correction to velocity fluxes after mesh reconnection.

### mesquiteMotionSolver
Class that provides a general interface to the Mesquite mesh smoothing library from Sandia National Labs. The class also performs smoothing for surface meshes using a spring-analogy approach, and is known to work in parallel.

## Target platform
This code is known to work with OpenFOAM.

## Author
    Sandeep Menon
    University of Massachusetts Amherst

## Disclaimer
This offering is not approved or endorsed by ESI or OpenCFD Ltd, the producer of the OpenFOAM software and owner of the OpenFOAM and OpenCFD trade marks.


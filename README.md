# Drace

The drace-client is a race detector for windows build on top of DynamoRIO.
It does not require any perparations like instrumentation of the binary to check.
However if debug information is not available, header-only C++11 constructs
cannot be detected correctly.

## Dependencies

- CMake > 3.8
- DynamoRIO 7.0.x
- C++11 / C99 Compiler

### External Libraries

**Mandatory:**

- [jtilly/inih](https://github.com/jtilly/inih)
- [leethomason/tinyxml2](https://github.com/leethomason/tinyxml2)
- [HowardHinnant/date](https://github.com/HowardHinnant/date)

**Optional:**

- [google/googletest](https://github.com/google/googletest)
- [google/benchmark](https://github.com/google/benchmark)
- [greq7mdp/sparsepp](https://github.com/greq7mdp/sparsepp)

## Using the Drace Race Detector

**Run the detector as follows**

```bash
drrun.exe -c drace-client.dll <detector parameter> -- application.exe <app parameter>
```

Currently the following parameters are implemented

*Instrumentation Parameters:*

```
-c <filename>     : path to config file. If not set `drace.ini` is used
-s <sampling-rate>: from all observed memory-references, analyze 1/n
-i <instr-rate>   : from the considered instructions, 1/n are actually instrumented
--lossy           : do not gather mem-refs from high-traffic application parts after some time
--lossy-flush     : remove instrumentation from high-traffic application parts after some time
                  : (only in combination with --lossy)
--yield-on-evt    : yield active thread after buffer is processed due to an event (e.g. mutex lock / unlock)
                    this might be necessary if more threads than cores are active
--excl-master     : exclude the runtime thread, useful if loader races
--delayed-syms    : do not lookup symbols on each race
--fast-mode       : only flush local buffers on sync-event (all buffers otherwise)
--stacksz         : size of callstack used for race-detection (must be in [1,16])
--xml-file <file> : log races in valkyries xml format in this file
--out-file <file> : log races in human readable format in this file
```

*Detector Parameters:*

```
--heap-only       : only detect races on heap-memory (exclude static memory)
```

### Symbol Resolving

Drace requires symbol information for wrapping functions and to resolve stack traces.
For the main functionality of C and C++ only applications, export information is sufficient.
However for additional and more precise race-detection (e.g. C++11, QT), debug information is necessary.

The application searches for this information in the path of the module and in `_NT_SYMBOL_PATH`.
However, only local symbols are searched (non `SRV` parts).

If symbols for system libs are necessary (e.g. for .Net), download them first from a symbol server.
Thereto it is useful to set the variable as follows:

```
set _NT_SYMBOL_PATH="c:\\symbolcache\\;SRV*c:\\symbolcache\\*https://msdl.microsoft.com/download/symbols"
```

### Dotnet

To correctly resolve managed stack frames, `mscordacwks.dll` is required.
Each Dotnet version is shipped with it's own dll, so copy the correct one from
the framework directory to the binary dir.

For .Net 4.0 the correct one can be found here

```
C:\Windows\Microsoft.NET\Framework64\v4.0.30319
```

## Testing with GoogleTest

Both the detector and a fully integrated DR-Client can be tested using the following command:

```
# Detector Tests
./test/drace-client-Tests.exe --dr <path-to-drrun.exe> --gtest_filter="Detector*"
# Integration Tests
./test/drace-client-Tests.exe --dr <path-to-drrun.exe> --gtest_filter="Dr*"
```

## Benchmarking with GoogleBenchmark

### Known Issues

- CSharp applications do not run on Windows 10 [#3046](https://github.com/DynamoRIO/dynamorio/issues/3046)
- TSAN can only be started once, as the cleanup is not fully working
- If using the SparsePP hashmap, the application might crash if a reallocation occurs which is not detected by DR correctly.


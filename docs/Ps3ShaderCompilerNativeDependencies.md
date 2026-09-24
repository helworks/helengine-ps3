# PS3 shader compiler native dependencies

The PS3 builder package carries SPIRV-Cross at
`runtimes/win-x64/native/spirv-cross.dll`, beside the packaged
`helengine.ps3.builder.dll`. Shader translation locates the native library
relative to the compiler assembly's fully qualified file location. This keeps
the lookup inside the externally registered builder package when the editor
hosts that builder in a different directory.

`AppContext.BaseDirectory` identifies the host process directory. When the
editor loads an external builder, that directory belongs to the editor and
does not identify the builder's package. The compiler therefore does not use
the process directory, current working directory, environment searches, or
system-installed copies to resolve SPIRV-Cross.

The compiler requires a nonempty, fully qualified assembly file location. A
missing or non-file-backed assembly location fails explicitly. If the package
does not contain `runtimes/win-x64/native/spirv-cross.dll` beside the compiler,
translation raises `FileNotFoundException` with the expected library path.

# PS3 native run duration

The standard PS3 build can opt into the native run-duration diagnostic with the process environment variable `HELENGINE_PS3_NATIVE_RUN_SECONDS`. Leave it unset for the existing unlimited run behavior. When set, provide an unsigned decimal integer from `0` through `3600`; invalid values fail before the native build process starts. The value is forwarded to `scripts/build.ps1` as separate `-RunSeconds` and seconds argument tokens.

`0` keeps the native loop unlimited. A positive value is compiled into the produced executable, and its timer starts after native initialization. Rebuild to change the duration. This setting does not alter an already-running game or force a hung startup to terminate.

For a bounded standard build, set the variable only for that invocation and restore the process's prior value afterward:

```powershell
$runSecondsName = 'HELENGINE_PS3_NATIVE_RUN_SECONDS'
$previousRunSeconds = [Environment]::GetEnvironmentVariable($runSecondsName, 'Process')
try {
    [Environment]::SetEnvironmentVariable($runSecondsName, '15', 'Process')
    powershell -NoProfile -ExecutionPolicy Bypass -File C:/dev/helworks/helengine/scripts/build-platform.ps1 `
      -Project C:/dev/helprojs/demodisc/project.heproj -Platform ps3 `
      -Output C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/standard-scene/output `
      -CacheRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/standard-scene/cache `
      -Configuration Release -BuildProfile release
} finally {
    [Environment]::SetEnvironmentVariable($runSecondsName, $previousRunSeconds, 'Process')
}
```

The argument tests verify validation and forwarding only. Real-scene acceptance still needs to observe `run-duration-reached`, successful drain, normal exit, and authored-owner retirement in the native run logs.

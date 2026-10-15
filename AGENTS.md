# Repository Guidelines

These are the sibling Helengine handler rules that apply to this native-only PS3 repository.

- One class per file. Keep native types focused and documented under `src/platform/ps3/`, in namespace `helengine::ps3`.
- Add substantive documentation comments (`///`) to every class, function and field. Explain the role and behavior; avoid placeholder text.
- Order members predictably: constants and fields, constructors, then methods.
- Use readable names and explicit errors. Do not mask failures with "best-effort" patches; preserve failure behavior and fix the root cause.
- Do not create default values when a valid value is required; fail visibly instead of silently constructing defaults.
- Never rewrite generated output. Shader binaries and headers produced by the SDK tools must not be edited after compilation; fix the source or the build rule instead.
- Ask before any major implementation that changes agreed scope. This milestone is native-only: no Core, generated runtime, authored scenes, editor integration, C# platform builder or shared-engine changes.
- Run the smallest validation necessary for the scope of the change.
- Do not take screenshots of Helena's screen or any windows unless she specifically states that screenshots are allowed for the task.
- Never write agent-owned build outputs, generated sources, build logs or build scripts to `%TEMP%` / `AppData\Local\Temp`. Use the visible workspace-owned build root `C:\dev\helworks\builds\helengine-ps3`. Always build into that same folder and overwrite the previous build; never keep a build history (no build-id, date, task or attempt folders). The only allowed split is one folder per platform.
- The PS3 SDK and cross-toolchain live only in the custom Docker image built from `docker/Dockerfile`. All native compilation and SDK shader conversion run inside that image. There is no host or WSL SDK fallback.
- Commit only the named files of a completed task after its validation passes; never `git add .`. Do not push or publish.

## Command Output
Protect context usage. **Any command with unknown or potentially large output must be byte-capped.**
Default pattern:
```bash
COMMAND 2>&1 | head -c 4000
```

using helengine;

namespace helengine.ps3.builder;

/// <summary>Compiles translated GLSL through the pinned PS3 SDK and validates the resulting RSX program.</summary>
public interface IPs3RsxCompiler {
    /// <summary>Compiles one translated shader inside the supplied build-owned working root.</summary>
    /// <param name="translation">Task2 GLSL translation and authored stage metadata.</param>
    /// <param name="workingRoot">Explicit root under which a unique compiler job directory is created.</param>
    /// <param name="cancellationToken">Token that stops this job and its uniquely named Docker container.</param>
    /// <returns>A validated immutable snapshot of the SDK program and dump.</returns>
    Ps3RsxProgram Compile(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken);
}

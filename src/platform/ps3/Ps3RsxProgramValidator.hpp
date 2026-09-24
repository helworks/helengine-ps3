#pragma once

#include "Ps3CookedShaderProgram.hpp"

#include <string>

namespace helengine::ps3 {

/// <summary>Checks raw RSX program structure and artifact-to-SDK resource identities without SDK helpers.</summary>
class Ps3RsxProgramValidator {
public:
    /// <summary>Validates both copied SDK blobs and every serialized binding before the program is published.</summary>
    /// <param name="program">Candidate CPU snapshot whose stage bytecode remains unchanged.</param>
    /// <param name="error">Receives the exact stage or resource that failed validation.</param>
    /// <returns>True only when all SDK ranges and source-to-SDK mappings are consistent.</returns>
    static bool Validate(const Ps3CookedShaderProgram& program, std::string& error);
};

} // namespace helengine::ps3

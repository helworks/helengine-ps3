## Existing frontend test change

Accepted Task1 fix1 source baseline compared with current source. Only the optional probe export should be removed; tests remain.

```diff
--- a/builder.tests/Ps3ShaderSourceCompilerTests.cs
+++ b/builder.tests/Ps3ShaderSourceCompilerTests.cs
@@ -22,21 +22,16 @@
     [Theory]
     [InlineData(ShaderStage.Vertex, "VS")]
     [InlineData(ShaderStage.Pixel, "PS")]
     public void ConvertsAuthoredHlsl(ShaderStage stage, string entry) {
         string glsl = new Ps3ShaderSourceCompiler().Compile(CreateRequest(Source, stage, entry), new Ps3ShaderIncludeResolverStub());
         Assert.Contains("#version 120", glsl);
         Assert.Contains("void main()", glsl);
         Assert.DoesNotContain("cbuffer", glsl);
-        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
-        if (!string.IsNullOrWhiteSpace(outputRoot)) {
-            Directory.CreateDirectory(outputRoot);
-            File.WriteAllText(Path.Combine(outputRoot, entry + ".glsl"), glsl);
-        }

     }
 
     /// <summary>Malformed source must produce a compiler diagnostic.</summary>
     [Fact]
     public void RejectsInvalidHlsl() {
         Assert.Throws<InvalidOperationException>(() => new Ps3ShaderSourceCompiler().Compile(CreateRequest("this is invalid", ShaderStage.Vertex, "VS"), new Ps3ShaderIncludeResolverStub()));
     }
 
```


const fs = require('fs')
const path = require('path')
const child_process = require('child_process')
const PATH = path.resolve(__dirname, "src")

fs.readdir(PATH, { withFileTypes: true }, async (err, files) => {
    if (err) throw err
    for (const file of files) {
        if (!file.isDirectory()) continue;
        console.log("-".repeat(process.stdout.columns))
        console.log(file.name)
        const dir = path.join(PATH, file.name)
        await fs.promises.writeFile(path.join(dir, "binding.gyp"), JSON.stringify({
            "targets": [
                {
                    "target_name": file.name,
                    "sources": ["main.cpp"],
                    "include_dirs": [
                        "<!(node -p \"require('node-addon-api').include\")",
                        "<!(node -p \"require('node-addon-api').include_dir\")"
                    ],
                    "cflags!": ["-fno-exceptions"],
                    "cflags_cc!": ["-fno-exceptions"],
                    "defines": ["NAPI_CPP_EXCEPTIONS"],
                }
            ]
        }, null, 2))
        const s = child_process.spawn('node-gyp.cmd', ['configure', '--arch=x64'], { cwd: dir, shell: true });
        s.stdout.pipe(process.stdout)
        s.stderr.pipe(process.stderr)
        await new Promise(r => s.on("close", r))
        await fs.promises.writeFile(path.join(dir, "build", file.name + ".vcxproj.user"), `<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
${['Debug|x64', 'Release|x64'].map(item => `  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='${item}'">
    <LocalDebuggerCommandArguments>/k "${process.execPath}" index.js</LocalDebuggerCommandArguments>
    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>
    <LocalDebuggerWorkingDirectory>$(ProjectDir)../</LocalDebuggerWorkingDirectory>
    <LocalDebuggerCommand>cmd.exe</LocalDebuggerCommand>
  </PropertyGroup>
`).join("")}</Project>`)
    }
})
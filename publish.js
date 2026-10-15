const fs = require('fs');
const path = require('path');
const dir = __dirname
const publishNodeDir = require("./publishNode").publishNodeDir;
fs.readdir(dir, { withFileTypes: true }, async (err, files) => {
    if (err) {
        console.log(err);
        return;
    }
    await fs.promises.mkdir(publishNodeDir, { recursive: true });
    const publishNodeFiles = new Set(await fs.promises.readdir(publishNodeDir));
    for (let file of files) {
        if (!file.isDirectory()) continue;
        const subDir = path.join(dir, file.name);
        for (const fileName of await fs.promises.readdir(subDir)) {
            if (fileName.endsWith(".node") && !publishNodeFiles.has(file.name + ".node")) {
                console.log(fileName, "->", path.join(publishNodeDir, fileName));
                await fs.promises.symlink(path.join(subDir, fileName), path.join(publishNodeDir, file.name + ".node"));
            }
        }
    }
})
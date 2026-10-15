const fs = require("fs");
const path = require("path");
const child_process = require("child_process");
const root = path.resolve(__dirname, "../../");
const zstdPath = path.resolve(root, "../zstd/lib");
const gyp = {
    targets: [
        {
            target_name: "zstd",
            product_dir: ".",
            sources: [
                "main.cpp",
                ...[
                    "common/entropy_common.c",
                    "common/error_private.c",
                    "common/fse_decompress.c",
                    "common/pool.c",
                    "common/threading.c",
                    "common/xxhash.c",
                    "common/zstd_common.c",
                    "compress/zstd_compress.c",
                    "compress/zstd_preSplit.c",
                    "compress/fse_compress.c",
                    "compress/huf_compress.c",
                    "compress/hist.c",
                    "compress/zstd_compress.c",
                    "compress/zstd_compress_literals.c",
                    "compress/zstd_compress_sequences.c",
                    "compress/zstd_compress_superblock.c",
                    "compress/zstd_fast.c",
                    "compress/zstd_double_fast.c",
                    "compress/zstd_lazy.c",
                    "compress/zstd_ldm.c",
                    "compress/zstd_opt.c",
                    "decompress/huf_decompress.c",
                    "decompress/zstd_decompress.c",
                    "decompress/zstd_decompress_block.c",
                    "decompress/zstd_decompress.c",
                    "decompress/zstd_decompress_block.c",
                    "decompress/zstd_ddict.c",
                    "compress/zstdmt_compress.c"
                ].map(item => path.resolve(zstdPath, item)),
            ],
            include_dirs: [path.resolve(root, "node_modules/node-addon-api"), zstdPath, path.resolve(zstdPath, "common")],
            "defines": [
                "ZSTD_MULTITHREAD"
            ],
            "cflags!": ["-fno-exceptions"],
            "cflags_cc!": ["-fno-exceptions"],
            msvs_settings: {
                VCCLCompilerTool: { ExceptionHandling: 1 },
            },
        },
    ],
};

const spawnSync = (...args) =>
    new Promise((resolve, reject) => {
        child_process
            .spawn("npx", args, {
                cwd: __dirname,
                shell: true,
                stdio: "inherit",
            })
            .once("exit", code => {
                if (code === 0) resolve();
                else reject(new Error(`spawn ${args.join(" ")} failed with exit code ${code}`));
            });
    });

fs.writeFile(path.resolve(__dirname, "binding.gyp"), JSON.stringify(gyp, null, 2), async () => {
    await spawnSync("node-gyp", "clean", "configure", "--release", "--arch=x64", "--force-process-config");
    console.log("-------------------configure success-------------------");
    await spawnSync("node-gyp", "build", "--release", "--arch=x64", "--force-process-config");
    console.log("-------------------build success-------------------");
    await fs.promises.rename(path.resolve(__dirname, "build", "zstd.node"), path.resolve(__dirname, "zstd.node"));
    await spawnSync("node-gyp", "clean");
    await fs.promises.unlink(path.resolve(__dirname, "binding.gyp"));
});
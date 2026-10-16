const fs = require("fs");
const path = require("path");
const child_process = require("child_process");
const root = path.resolve(__dirname);
const cwd = path.resolve("./");

const projectSet = new Set(
  fs
    .readdirSync(root, { withFileTypes: true })
    .filter(item => item.isDirectory() && !item.name.startsWith("."))
    .map(item => item.name),
);

const projects = process.argv.slice(2).filter(item => projectSet.has(item));

if (projects.length === 0 && cwd !== root && cwd.startsWith(root)) {
  const projectName = path.parse(cwd).base;
  if (projectSet.has(projectName)) projects.push(projectName);
}

if (projects.length === 0) {
  for (const projectName of new Set(
    String(child_process.execFileSync("git", ["status", "-s"], { cwd: root }))
      .split("\n")
      .map(item => item.trim().match(/ ([^/]+)\//)?.[1])
      .filter(p => p && projectSet.has(p)),
  )) {
    projects.push(projectName);
  }
}

console.log(projects);
(async () => {
  for (const projectName of projects) {
    await buildProject(projectName);
  }
  child_process.spawn("node", ["publish.js"], { cwd: root, stdio: "inherit" });
})();

async function buildProject(projectName) {
  const dir = path.join(root, projectName);
  const fileSet = new Set(await fs.promises.readdir(dir));
  if (!fileSet.has("main.cpp")) return;
  const gyp = {
    targets: [
      {
        target_name: projectName,
        product_dir: ".",
        sources: ["main.cpp"],
        include_dirs: [path.resolve(root, "node_modules/node-addon-api")],
        "cflags!": ["-fno-exceptions"],
        "cflags_cc!": ["-fno-exceptions"],
        msvs_settings: {
          VCCLCompilerTool: { ExceptionHandling: 1 },
        },
      },
    ],
  };

  const extraGyp = fileSet.has("build.js")
    ? require(path.resolve(dir, "build.js"))
    : fileSet.has("build.json")
      ? JSON.parse(String(await fs.promises.readFileSync(path.resolve(dir, "build.json"))))
      : {};

  mergeObject(gyp.targets[0], extraGyp);

  const spawnSync = (...args) =>
    new Promise((resolve, reject) => {
      child_process
        .spawn("npx", args, {
          cwd: dir,
          shell: true,
          stdio: "inherit",
        })
        .once("exit", code => {
          if (code === 0) resolve();
          else reject(new Error(`spawn ${args.join(" ")} failed with exit code ${code}`));
        });
    });

  fs.writeFile(path.resolve(dir, "binding.gyp"), JSON.stringify(gyp, null, 2), async () => {
    await spawnSync("node-gyp", "clean", "configure", "--release", "--arch=x64", "--force-process-config");
    console.log(`-----------${projectName}--------configure success-------------------`);
    await spawnSync("node-gyp", "build", "--release", "--arch=x64", "--force-process-config");
    console.log(`-----------${projectName}--------build success-------------------`);
    await fs.promises.rename(
      path.resolve(dir, "build", `${projectName}.node`),
      path.resolve(dir, `${projectName}.node`),
    );
    await spawnSync("node-gyp", "clean");
    await fs.promises.unlink(path.resolve(dir, "binding.gyp"));
  });
}

/**
 * 递归合并两个对象，直接修改 target，保留已有对象的引用。
 *
 * @param target 目标对象，会被直接修改
 * @param source 来源对象，提供需要合并的数据
 * @returns 合并后的 target 对象
 */
function mergeObject(target, source) {
  for (const key of Object.keys(source)) {
    const sourceValue = source[key];
    const targetValue = target[key];

    if (Array.isArray(targetValue) && Array.isArray(sourceValue)) {
      // 两边都是数组时，将 source 的元素追加到 target，保留数组引用
      targetValue.push(...sourceValue);
    } else if (
      sourceValue !== null &&
      targetValue !== null &&
      typeof sourceValue === "object" &&
      typeof targetValue === "object" &&
      !Array.isArray(sourceValue) &&
      !Array.isArray(targetValue)
    ) {
      // 两边都是普通对象时，递归合并，保留 target 的嵌套对象引用
      mergeObject(targetValue, sourceValue);
    } else {
      // 基本类型、数组或其他类型直接覆盖
      target[key] = sourceValue;
    }
  }

  return target;
}

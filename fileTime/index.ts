const addon = require("../publishNode").require("fileTime.node");
const fileTime = addon.fileTime as (file: string, ctime: number, mtime: number, atime: number) => void;
export default fileTime;
// 测试用例
// import * as fs from "fs";
// import * as path from "path";
// const file = path.resolve(__dirname, "中文 文件.txt");
// const now = new Date().getTime();
// const ctime = now - 365 * 24 * 60 * 60 * 1000;
// const mtime = now - 30 * 24 * 60 * 60 * 1000;
// const atime = now - 7 * 24 * 60 * 60 * 1000;
// fileTime(file, ctime, mtime, atime);
// const stat = fs.statSync(file);
// console.log(stat);
// console.log(new Date(stat.birthtimeMs).toLocaleString());
// console.log(new Date(stat.mtimeMs).toLocaleString());
// console.log(new Date(stat.atimeMs).toLocaleString());

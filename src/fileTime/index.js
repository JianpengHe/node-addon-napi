const addon = require('./build/Release/fileTime.node');
const fs = require('fs')
const now = new Date().getTime()
const ctime = now - 365 * 24 * 60 * 60 * 1000
const mtime = now - 30 * 24 * 60 * 60 * 1000
const atime = now - 7 * 24 * 60 * 60 * 1000
addon.fileTime(__filename, ctime, mtime, atime)
const stat = fs.statSync(__filename)
console.log(stat)
console.log(new Date(stat.birthtimeMs).toLocaleString())
console.log(new Date(stat.mtimeMs).toLocaleString())
console.log(new Date(stat.atimeMs).toLocaleString())

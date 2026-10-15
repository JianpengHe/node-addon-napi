const path = require('path');
const os = require('os');
const publishNodeDir = path.join(os.tmpdir(), "node-addon")
module.exports = {
    publishNodeDir,
    require(name) {
        return (typeof __non_webpack_require__ !== 'undefined' ? __non_webpack_require__ : require)(path.join(publishNodeDir, name));
    }
}
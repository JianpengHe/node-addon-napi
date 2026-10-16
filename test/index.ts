const addon = require("../publishNode").require("test.node");
const hello = addon.hello as () => void;
export default hello;

console.log(hello()); // Hello from N-API!

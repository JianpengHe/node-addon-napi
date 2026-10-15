"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.ZstdDecompress = exports.ZstdCompress = void 0;
const stream = __importStar(require("stream"));
const path = __importStar(require("path"));
const addon = globalThis["require"](path.resolve(__dirname, "zstd.node"));
class ZstdCompress extends stream.Transform {
    codec;
    finishedBytes = 0;
    constructor(compressionLevel = 3, threadCount = 0) {
        super();
        this.codec = new addon.ZstdCompressor(compressionLevel, threadCount);
    }
    _transform(chunk, encoding, callback) {
        try {
            const output = this.codec.compress(chunk);
            if (output.length > 0)
                this.push(output);
            callback();
        }
        catch (err) {
            callback(err);
        }
        this.finishedBytes += chunk.length;
    }
    _flush(callback) {
        try {
            const output = this.codec.finish();
            if (output.length > 0)
                this.push(output);
            callback();
        }
        catch (err) {
            callback(err);
        }
    }
    _destroy(err, callback) {
        try {
            this.codec.close();
        }
        catch (closeErr) {
            err ||= closeErr;
        }
        callback(err);
    }
}
exports.ZstdCompress = ZstdCompress;
class ZstdDecompress extends stream.Transform {
    codec;
    finishedBytes = 0;
    constructor() {
        super();
        this.codec = new addon.ZstdDecompressor();
    }
    _transform(chunk, encoding, callback) {
        try {
            const output = this.codec.decompress(chunk);
            if (output.length > 0)
                this.push(output);
            callback();
        }
        catch (err) {
            callback(err);
        }
    }
    _flush(callback) {
        try {
            const output = this.codec.finish();
            if (output.length > 0)
                this.push(output);
            callback();
        }
        catch (err) {
            callback(err);
        }
    }
    _destroy(err, callback) {
        try {
            this.codec.close();
        }
        catch (closeErr) {
            err ||= closeErr;
        }
        callback(err);
    }
}
exports.ZstdDecompress = ZstdDecompress;
//# sourceMappingURL=index.js.map
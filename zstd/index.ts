import * as stream from "stream";
import * as path from "path";

const addon = globalThis["require"](path.resolve(__dirname, "zstd.node"));

/**
 * Zstd 压缩 Transform。
 */
export class ZstdCompress extends stream.Transform {
  private codec: any;
  /** 已处理的字节数 */
  public finishedBytes: number = 0;
  constructor(compressionLevel = 3, threadCount = 0) {
    super();
    // 将压缩级别传给 C++，默认使用 3 级压缩。
    this.codec = new addon.ZstdCompressor(compressionLevel, threadCount);
  }

  _transform(chunk: Buffer, encoding: string, callback: (err?: any) => void) {
    try {
      const output = this.codec.compress(chunk);
      if (output.length > 0) this.push(output);
      callback();
    } catch (err: any) {
      callback(err);
    }
    this.finishedBytes += chunk.length;
  }

  _flush(callback: (err?: any) => void) {
    try {
      // 结束压缩帧，确保最后的数据和帧尾全部输出。
      const output = this.codec.finish();
      if (output.length > 0) this.push(output);
      callback();
    } catch (err) {
      callback(err);
    }
  }

  _destroy(err: any, callback: (err?: any) => void) {
    try {
      this.codec.close();
    } catch (closeErr: any) {
      err ||= closeErr;
    }
    callback(err);
  }
}

/**
 * Zstd 解压 Transform。
 *
 * 支持分块解压，并在输入结束时检查压缩帧是否完整。
 */
export class ZstdDecompress extends stream.Transform {
  private codec: any;
  /** 已处理的字节数 */
  public finishedBytes: number = 0;
  constructor() {
    super();
    this.codec = new addon.ZstdDecompressor();
  }

  _transform(chunk: Buffer, encoding: string, callback: (err?: any) => void) {
    try {
      const output = this.codec.decompress(chunk);
      if (output.length > 0) this.push(output);
      callback();
    } catch (err) {
      callback(err);
    }
  }

  _flush(callback: (err?: any) => void) {
    try {
      // 检查是否完整读取并解压了 Zstd 帧。
      const output = this.codec.finish();
      if (output.length > 0) this.push(output);
      callback();
    } catch (err) {
      callback(err);
    }
  }

  _destroy(err: any, callback: (err?: any) => void) {
    try {
      this.codec.close();
    } catch (closeErr: any) {
      err ||= closeErr;
    }
    callback(err);
  }
}

/**
 * 将一个文件压缩为 Zstd 格式。
 */
//  function compressFile(inputPath, outputPath) {
//   await pipeline(fs.createReadStream(inputPath), new ZstdCompress(16), fs.createWriteStream(outputPath));
//   return new Promise((resolve, reject) => {
//       const zstdStream = child_process.spawn("zstd.exe", ["-16", "-vc", "-T0", "--progress", "--no-check"], {
//           stdio: ["pipe", "overlapped", "inherit"],
//       });
//       zstdStream.on('exit', (code) => {
//           if (code === 0) {
//               resolve();
//           } else {
//               reject(new Error(`Zstd compression failed with exit code ${code}`));
//           }
//       });

//       fs.createReadStream(inputPath).pipe(zstdStream.stdin);
//       zstdStream.stdout.pipe(fs.createWriteStream(outputPath));
//   });
// }

/**
 * 将一个 Zstd 文件解压到目标文件。
 */
// async function decompressFile(inputPath, outputPath) {
//   await pipeline(fs.createReadStream(inputPath), new ZstdDecompressTransform(), fs.createWriteStream(outputPath));
// }

// async function main() {
//   console.time("1");
//   // await compressFile('node_modules.zip', 'node_modules.zip.zst');
//   // console.log('压缩完成');
//   // await decompressFile('node_modules.zip.zst', 'node_modules-restored.zip');
//   // console.log('解压完成');
//   console.timeEnd("1");
// }

// main().catch(console.error);

import * as stream from "stream";
const addon = require("../publishNode").require("zstd.node");

export type IZstdProgressCallback = (isEnd: boolean, inputBytes: number, outputBytes: number) => void;
class ZstdBase extends stream.Transform {
  /** 输出字节数 */
  public outputBytes: number = 0;
  /** 输入字节数 */
  public inputBytes: number = 0;
  constructor(
    protected codec: any,
    protected readonly onProgress?: IZstdProgressCallback,
  ) {
    super();
  }
  _transform(chunk: Buffer, encoding: string, callback: (err?: any) => void) {
    this.codec.transform(chunk, (err, output) => {
      if (err) return callback(err);
      this.outputBytes += output.length;
      this.inputBytes += chunk.length;
      if (output.length > 0) {
        this.push(output);
        callback();
        this.onProgress?.(false, this.inputBytes, this.outputBytes);
        return;
      }
      callback();
    });
  }

  _flush(callback: (err?: any) => void) {
    this.codec.flush((err, output) => {
      if (err) return callback(err);
      this.outputBytes += output.length;

      if (output.length > 0) this.push(output);
      // 必须输出最后的帧尾数据
      callback();
      this.onProgress?.(true, this.inputBytes, this.outputBytes);
    });
  }

  _destroy(err: any, callback: (err?: any) => void) {
    try {
      this.codec.destroy();
    } catch (closeErr: any) {
      err ||= closeErr;
    }
    callback(err);
  }
}

/**
 * Zstd 压缩 Transform。
 */
export class ZstdCompress extends ZstdBase {
  constructor(onProgress?: IZstdProgressCallback, compressionLevel = 3, threadCount = 0) {
    super(new addon.ZstdCompressor(compressionLevel, threadCount), onProgress);
  }
}

/**
 * Zstd 解压 Transform。
 *
 * 支持分块解压，并在输入结束时检查压缩帧是否完整。
 */
export class ZstdDecompress extends ZstdBase {
  constructor(onProgress?: IZstdProgressCallback) {
    super(new addon.ZstdDecompressor(), onProgress);
  }
}

export function zstdRateLimitProgress<Callback extends (...args: any[]) => void>(
  interval: number,
  callback: Callback,
): Callback {
  let timer: NodeJS.Timeout | null = null;
  let curArgs: Parameters<Callback> | undefined;
  const fn = (...args: Parameters<Callback>): void => {
    if (timer) {
      curArgs = args;
      return;
    }
    callback(...args);
    curArgs = undefined;
    timer = setTimeout(() => {
      timer = null;
      if (curArgs) fn(...curArgs);
    }, interval);
  };
  return fn as unknown as Callback;
}

// 测试用例
// import { pipeline } from "stream/promises";
// import * as fs from "fs";
// import * as child_process from "child_process";

// const showProgress: IZstdProgressCallback = (isEnd: boolean, inputBytes: number, outputBytes: number) =>
//   console.log("进度:", inputBytes, outputBytes, "压缩率" + ((outputBytes / inputBytes) * 100).toFixed(2) + "%");

// async function compressFile(readStream: stream.Readable, writeStream: stream.Writable) {
//   console.log("压缩");
//   const zstdStream = new ZstdCompress(zstdRateLimitProgress(500, showProgress), 16);
//   await pipeline(readStream, zstdStream, writeStream);

//   // const zstdStream = child_process.spawn("zstd.exe", ["-16", "-vc", "-T0", "--progress", "--no-check"], {
//   //   stdio: ["pipe", "overlapped", "inherit"],
//   // });
//   // return new Promise<void>((resolve, reject) => {
//   //   zstdStream.on("exit", code =>
//   //     code === 0 ? resolve() : reject(new Error(`Zstd compression failed with exit code ${code}`)),
//   //   );
//   //   readStream.pipe(zstdStream.stdin);
//   //   zstdStream.stdout.pipe(writeStream);
//   // });
// }

// /**
//  * 将一个 Zstd 文件解压到目标文件。
//  */
// async function decompressFile(readStream: stream.Readable, writeStream: stream.Writable) {
//   console.log("解压");
//   const zstdStream = new ZstdDecompress(zstdRateLimitProgress(500, showProgress));
//   await pipeline(readStream, zstdStream, writeStream);
// }

// async function main() {
//   console.time("time");
//   await compressFile(fs.createReadStream("node_modules.zip"), fs.createWriteStream("node_modules.zip.zst"));
//   // console.log('压缩完成');
//   await decompressFile(fs.createReadStream("node_modules.zip.zst"), fs.createWriteStream("node_modules-restored.zip"));
//   // console.log('解压完成');
//   console.timeEnd("time");
// }

// main().catch(console.error);
